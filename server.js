// API: Đăng ký slot
app.post('/register', async (req, res) => {
  const { slot_number, license_plate } = req.body;
  console.log(`📥 Register request: slot=${slot_number}, plate=${license_plate}`);
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
      'INSERT INTO logs (license_plate, otp) VALUES (?, ?)',
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
  console.log(`🚗 Check-in: plate=${license_plate}, otp=${otp}`);

  try {
    const [rows] = await db.query(
      'SELECT * FROM slots WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );
    if (!rows.length) return res.status(400).json({ error: 'Invalid license_plate or otp' });

    // Update logs: đặt time_in khi check-in thực sự
    await db.query(
      'UPDATE logs SET time_in=NOW() WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );

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
  console.log(`🚗 Check-out: plate=${license_plate}, otp=${otp}`);

  try {
    const [rows] = await db.query(
      'SELECT * FROM slots WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );
    if (!rows.length) return res.status(400).json({ error: 'Invalid license_plate or otp' });

    // Kiểm tra đã check-in chưa (phải có time_in)
    const [logRows] = await db.query(
      'SELECT time_in FROM logs WHERE license_plate=? AND otp=?',
      [license_plate, otp]
    );
    if (!logRows.length || !logRows[0].time_in) {
      return res.status(400).json({ error: 'Chưa check-in, không thể check-out' });
    }

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