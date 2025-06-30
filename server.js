require('dotenv').config();
const express = require('express');
const mysql = require('mysql2/promise');
const cors = require('cors');

const app = express();
app.use(cors());
app.use(express.json());

// Kết nối pool MySQL
const db = mysql.createPool({
  host: process.env.DB_HOST,
  user: process.env.DB_USER,
  password: process.env.DB_PASSWORD,
  database: process.env.DB_NAME,
});

// Kiểm tra kết nối ngay khi server khởi động
(async () => {
  try {
    const connection = await db.getConnection();
    console.log('✅ MySQL connected successfully');
    connection.release(); // trả lại kết nối cho pool
  } catch (err) {
    console.error('❌ MySQL connection failed:', err);
    process.exit(1); // thoát nếu lỗi kết nối
  }
})();

// API: Lấy danh sách slot
app.get('/slots', async (req, res) => {
  try {
    const [rows] = await db.query('SELECT * FROM slots');
    res.json(rows);
  } catch (err) {
    console.error(err);
    res.status(500).json({error: 'DB error'});
  }
});

// API: Đăng ký slot
app.post('/register', async (req, res) => {
  const { slot_number, license_plate } = req.body;
  if (!slot_number || !license_plate) {
    return res.status(400).json({ error: 'Missing slot_number or license_plate' });
  }
  const otp = Math.floor(100000 + Math.random() * 900000).toString();

  try {
    const [rows] = await db.query(
      'SELECT status FROM slots WHERE slot_number = ?', [slot_number]
    );
    if (!rows.length) return res.status(404).json({ error: 'Slot not found' });
    if (rows[0].status === 'occupied') return res.status(400).json({ error: 'Slot occupied' });

    await db.query(
      'UPDATE slots SET status="occupied", license_plate=?, otp=? WHERE slot_number=?',
      [license_plate, otp, slot_number]
    );
    await db.query(
      'INSERT INTO logs (license_plate, otp, time_in) VALUES (?, ?, NOW())',
      [license_plate, otp]
    );
    res.json({ message: 'Registered successfully', otp });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// API: Check-in
app.post('/checkin', async (req, res) => {
  const { license_plate, otp } = req.body;
  if (!license_plate || !otp) return res.status(400).json({ error: 'Missing license_plate or otp' });

  try {
    const [rows] = await db.query(
      'SELECT * FROM slots WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );
    if (!rows.length) return res.status(400).json({ error: 'Invalid license_plate or otp' });
    res.json({ message: 'Check-in success' });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// API: Check-out
app.post('/checkout', async (req, res) => {
  const { license_plate, otp } = req.body;
  if (!license_plate || !otp) return res.status(400).json({ error: 'Missing license_plate or otp' });

  try {
    const [rows] = await db.query(
      'SELECT * FROM slots WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );
    if (!rows.length) return res.status(400).json({ error: 'Invalid license_plate or otp' });

    const slotNumber = rows[0].slot_number;

    await db.query(
      'UPDATE slots SET status="available", license_plate=NULL, otp=NULL WHERE slot_number=?',
      [slotNumber]
    );
    await db.query(
      'UPDATE logs SET time_out=NOW() WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );
    res.json({ message: 'Check-out success' });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'DB error' });
  }
});

// Start server
const port = process.env.PORT || 3000;
app.listen(port, () => console.log(`Server running at http://localhost:${port}`));