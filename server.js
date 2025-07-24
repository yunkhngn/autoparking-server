require('dotenv').config();
const express = require('express');
const mysql = require('mysql2/promise');
const cors = require('cors');
const axios = require('axios');

const app = express();

app.use(cors());
app.use(express.json());

const db = mysql.createPool({
  host: process.env.DB_HOST,
  user: process.env.DB_USER,
  password: process.env.DB_PASSWORD,
  database: process.env.DB_NAME,
});

(async () => {
  try {
    const connection = await db.getConnection();
    console.log('MySQL connected successfully');
    connection.release();
  } catch (err) {
    console.error('MySQL connection failed:', err);
    process.exit(1);
  }
})();

// GET slot
app.get('/slots', async (req, res) => {
  try {
    const [rows] = await db.query('SELECT * FROM slots');
    res.json(rows);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// POST /register
app.post('/register', async (req, res) => {
  const { slot_number, license_plate } = req.body;
  console.log(`Register request: slot=${slot_number}, plate=${license_plate}`);

  if (!slot_number || !license_plate) {
    return res.status(400).json({ error: 'Missing slot_number or license_plate' });
  }

  const otp = Math.floor(100000 + Math.random() * 900000).toString();

  try {
    const [rows] = await db.query(
      'SELECT status FROM slots WHERE slot_number = ?',
      [slot_number]
    );

    if (!rows.length) return res.status(404).json({ error: 'Slot not found' });
    if (rows[0].status === 'occupied') return res.status(400).json({ error: 'Slot occupied' });

    await db.query(
      'UPDATE slots SET status="occupied", license_plate=?, otp=? WHERE slot_number=?',
      [license_plate, otp, slot_number]
    );

    await db.query(
      'INSERT INTO logs (license_plate, otp) VALUES (?, ?)',
      [license_plate, otp]
    );

    try {
      await axios.post('http://192.168.4.1/esp-led', {
        slot: slot_number,
        status: 'registered'
      });
      console.log('ESP LED update sent for registered slot');
    } catch (err) {
      console.warn('Không gửi được yêu cầu cập nhật LED:', err.message);
    }

    res.json({ message: 'Registered successfully', otp });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// POST /checkin
app.post('/checkin', async (req, res) => {
  const { license_plate, otp } = req.body;

  if (!license_plate || !otp) return res.status(400).json({ error: 'Missing license_plate or otp' });

  console.log(`Check-in: plate=${license_plate}, otp=${otp}`);

  try {
    const [rows] = await db.query(
      'SELECT * FROM slots WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );

    if (!rows.length) return res.status(400).json({ error: 'Invalid license_plate or otp' });

    // Thêm kiểm tra cảm biến cổng
    const gateStatus = await axios.get('http://192.168.4.1/check-gate');
    if (!gateStatus.data || gateStatus.data.has_vehicle !== true) {
      return res.status(403).json({ error: 'Không có xe tại cổng, không thể check-in' });
    }

    await db.query(
      'UPDATE logs SET time_in=NOW() WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );

    await db.query(
      'UPDATE slots SET status="occupied" WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );

    const slotNumber = rows[0].slot_number;

    // Gửi yêu cầu mở cổng tới ESP32
    try {
      await axios.post('http://192.168.4.1/esp-checkin', {
        slot: slotNumber
      });
      console.log('ESP check-in gate triggered');
    } catch (espErr) {
      console.error('Gửi yêu cầu mở cổng thất bại:', espErr.message);
    }

    res.json({ message: 'Check-in success' });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// POST /checkout
app.post('/checkout', async (req, res) => {
  const { license_plate, otp } = req.body;

  if (!license_plate || !otp) return res.status(400).json({ error: 'Missing license_plate or otp' });

  console.log(`Check-out: plate=${license_plate}, otp=${otp}`);

  try {
    const [rows] = await db.query(
      'SELECT * FROM slots WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );

    if (!rows.length) return res.status(400).json({ error: 'Invalid license_plate or otp' });

    const [logRows] = await db.query(
      'SELECT time_in FROM logs WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );

    if (!logRows.length || !logRows[0].time_in) {
      return res.status(400).json({ error: 'Chưa check-in, không thể check-out' });
    }

    const slotNumber = rows[0].slot_number;

    // Thêm kiểm tra slot vẫn còn xe
    const statusRes = await axios.get(`http://192.168.4.1/slot-status?slot=${slotNumber}`);
    if (statusRes.data.occupied) {
      return res.status(403).json({ error: 'Xe vẫn đang ở slot, không thể check-out' });
    }

    // Gửi request đến ESP32 trước
    try {
      const connection = await db.getConnection();

      try {
        await connection.beginTransaction();

        await connection.query(
          'UPDATE slots SET status="available", license_plate=NULL, otp=NULL WHERE slot_number=?',
          [slotNumber]
        );

        await connection.query(
          'UPDATE logs SET time_out=NOW() WHERE license_plate=? AND otp=?',
          [license_plate, otp]
        );

        await connection.commit();

        console.log('DB updated before ESP checkout');
      } catch (dbErr) {
        await connection.rollback();
        console.error('DB update failed:', dbErr.message);
        return res.status(500).json({ error: 'DB update failed' });
      } finally {
        connection.release();
      }

      try {
        const espRes = await axios.post('http://192.168.4.1/esp-checkout', {
          slot: slotNumber
        });

        if (espRes.status === 200) {
          console.log('ESP check-out gate triggered');
          res.json({ message: 'Check-out success' });
        } else {
          console.warn('ESP trả về không hợp lệ:', espRes.status);
          res.json({ message: 'Check-out success (ESP returned invalid response)' });
        }
      } catch (espErr) {
        console.error('Gửi yêu cầu mở cổng thất bại:', espErr.message);
        res.json({ message: 'Check-out success (ESP failed)' });
      }
    } catch (espErr) {
      console.error('Gửi yêu cầu mở cổng thất bại:', espErr.message);

      const connection = await db.getConnection();

      try {
        await connection.beginTransaction();

        await connection.query(
          'UPDATE slots SET status="available", license_plate=NULL, otp=NULL WHERE slot_number=?',
          [slotNumber]
        );

        await connection.query(
          'UPDATE logs SET time_out=NOW() WHERE license_plate=? AND otp=?',
          [license_plate, otp]
        );

        await connection.commit();

        console.log('DB updated despite ESP error');
        res.json({ message: 'Check-out success (ESP failed)' });
      } catch (dbErr) {
        await connection.rollback();
        console.error('error: DB update failed after ESP error:', dbErr.message);
        return res.status(500).json({ error: 'DB update failed' });
      } finally {
        connection.release();
      }
    }
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// GET /logs
app.get('/logs', async (req, res) => {
  try {
    const [rows] = await db.query('SELECT * FROM logs ORDER BY time_in DESC');
    res.json(rows);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// GET /esp-slots
app.get('/esp-slots', async (req, res) => {
  try {
    const [rows] = await db.query('SELECT slot_number, status FROM slots');
    res.json(rows);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// POST /status
app.post('/status', async (req, res) => {
  const { license_plate, otp } = req.body;

  if (!license_plate || !otp) return res.status(400).json({ error: 'Missing license_plate or otp' });

  try {
    const [rows] = await db.query(
      'SELECT time_in, time_out FROM logs WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );

    if (!rows.length) return res.status(400).json({ error: 'Không tìm thấy đăng ký này' });

    const log = rows[0];

    if (log.time_in && !log.time_out) {
      return res.json({ status: 'checked_in' });
    } else if (log.time_in && log.time_out) {
      return res.json({ status: 'checked_out' });
    } else {
      return res.json({ status: 'not_checked_in' });
    }
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// Start
const port = process.env.PORT || 1204;
app.listen(port, () => console.log(`Server running at http://localhost:${port}`));