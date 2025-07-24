require('dotenv').config();
const express = require('express');
const mysql = require('mysql2/promise');
const cors = require('cors');
const axios = require('axios');

console.clear();

const app = express();

const startTime = Date.now();
const getUptime = () => {
  const ms = Date.now() - startTime;
  const s = Math.floor(ms / 1000) % 60;
  const m = Math.floor(ms / 1000 / 60) % 60;
  const h = Math.floor(ms / 1000 / 60 / 60);
  return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
};


const log = (type, msg, color = '\x1b[0m') => {
  const now = new Date().toLocaleTimeString();
  const bold = '\x1b[1m';
  const reset = '\x1b[0m';
  console.log(`${now} | ${bold}${color}[${type}]${reset} ${msg}`);
};


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
    log('INFO', 'MySQL connected successfully', '\x1b[36m');
    connection.release();
  } catch (err) {
    log('ERROR', 'MySQL connection failed: ' + err.message, '\x1b[31m');
    process.exit(1);
  }
})();

// GET slot
app.get('/slots', async (req, res) => {
  try {
    const [rows] = await db.query('SELECT * FROM slots');
    res.json(rows);
  } catch (err) {
    log('ERROR', err.message, '\x1b[31m');
    res.status(500).json({ error: 'DB error' });
  }
});

// POST /register
app.post('/register', async (req, res) => {
  const { slot_number, license_plate } = req.body;
  log('INFO', `Register request: slot=${slot_number}, plate=${license_plate}`, '\x1b[36m');

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
      log('INFO', 'ESP LED update sent for registered slot', '\x1b[36m');
    } catch (err) {
      log('WARN', 'Không gửi được yêu cầu cập nhật LED: ' + err.message, '\x1b[33m');
    }

    res.json({ message: 'Registered successfully', otp });
  } catch (err) {
    log('ERROR', err.message, '\x1b[31m');
    res.status(500).json({ error: 'DB error' });
  }
});

// POST /checkin
app.post('/checkin', async (req, res) => {
  const { license_plate, otp } = req.body;

  if (!license_plate || !otp) return res.status(400).json({ error: 'Missing license_plate or otp' });

  log('INFO', `Check-in: plate=${license_plate}, otp=${otp}`, '\x1b[36m');

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
      log('INFO', 'ESP check-in gate triggered', '\x1b[36m');
    } catch (espErr) {
      log('ERROR', 'Gửi yêu cầu mở cổng thất bại: ' + espErr.message, '\x1b[31m');
    }

    res.json({ message: 'Check-in success' });
  } catch (err) {
    log('ERROR', err.message, '\x1b[31m');
    res.status(500).json({ error: 'DB error' });
  }
});

// POST /checkout
app.post('/checkout', async (req, res) => {
  const { license_plate, otp } = req.body;

  if (!license_plate || !otp) return res.status(400).json({ error: 'Missing license_plate or otp' });

  log('INFO', `Check-out: plate=${license_plate}, otp=${otp}`, '\x1b[36m');

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
      } catch (dbErr) {
        await connection.rollback();
        log('ERROR', 'DB update failed: ' + dbErr.message, '\x1b[31m');
        return res.status(500).json({ error: 'DB update failed' });
      } finally {
        connection.release();
      }

      try {
        const espRes = await axios.post('http://192.168.4.1/esp-checkout', {
          slot: slotNumber
        });

        if (espRes.status === 200) {
          log('INFO', 'ESP check-out gate triggered', '\x1b[36m');
          res.json({ message: 'Check-out success' });
        } else {
          log('WARN', 'ESP trả về không hợp lệ: ' + espRes.status, '\x1b[33m');
          res.json({ message: 'Check-out success (ESP returned invalid response)' });
        }
      } catch (espErr) {
        log('ERROR', 'Gửi yêu cầu mở cổng thất bại: ' + espErr.message, '\x1b[31m');
        res.json({ message: 'Check-out success (ESP failed)' });
      }
    } catch (espErr) {
      log('ERROR', 'Gửi yêu cầu mở cổng thất bại: ' + espErr.message, '\x1b[31m');

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

        log('INFO', 'DB updated despite ESP error', '\x1b[36m');
        res.json({ message: 'Check-out success (ESP failed)' });
      } catch (dbErr) {
        await connection.rollback();
        log('ERROR', 'error: DB update failed after ESP error: ' + dbErr.message, '\x1b[31m');
        return res.status(500).json({ error: 'DB update failed' });
      } finally {
        connection.release();
      }
    }
  } catch (err) {
    log('ERROR', err.message, '\x1b[31m');
    res.status(500).json({ error: 'DB error' });
  }
});

// GET /logs
app.get('/logs', async (req, res) => {
  try {
    const [rows] = await db.query('SELECT * FROM logs ORDER BY time_in DESC');
    res.json(rows);
  } catch (err) {
    log('ERROR', err.message, '\x1b[31m');
    res.status(500).json({ error: 'DB error' });
  }
});

// GET /esp-slots
app.get('/esp-slots', async (req, res) => {
  try {
    const [rows] = await db.query('SELECT slot_number, status FROM slots');
    res.json(rows);
  } catch (err) {
    log('ERROR', err.message, '\x1b[31m');
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

    const logEntry = rows[0];

    if (logEntry.time_in && !logEntry.time_out) {
      return res.json({ status: 'checked_in' });
    } else if (logEntry.time_in && logEntry.time_out) {
      return res.json({ status: 'checked_out' });
    } else {
      return res.json({ status: 'not_checked_in' });
    }
  } catch (err) {
    log('ERROR', err.message, '\x1b[31m');
    res.status(500).json({ error: 'DB error' });
  }
});

// Start
const port = process.env.PORT || 1204;
app.listen(port, () => log('INFO', `Server running at http://localhost:${port}`, '\x1b[36m'));