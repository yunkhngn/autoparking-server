# AutoParking - Smart Parking Management IoT System

A smart parking system using ESP32, React, and Node.js that enables automatic parking lot management with slot registration, check-in/check-out, and real-time monitoring features.

## Table of Contents

- [Introduction](#introduction)
- [Features](#features)
- [System Architecture](#system-architecture)
- [Hardware](#hardware)
- [Installation](#installation)
- [Usage](#usage)
- [API Documentation](#api-documentation)
- [Project Structure](#project-structure)
- [Operation Flow](#operation-flow)
- [Author](#author)

## Introduction

AutoParking is a complete IoT system for smart parking lot management, including:

- **ESP32 Firmware**: Controls hardware (sensors, LEDs, servo) and provides captive portal
- **Web Client (React)**: User interface for slot registration and management
- **Backend Server (Node.js)**: API server handling business logic and data storage

## Features

### User Features
- Online parking slot registration
- Receive OTP for authentication
- Automatic check-in/check-out
- Real-time slot status monitoring
- License plate validation (supports formats: `ABC-12345`, `29M-1234`)

### System Features
- Wi-Fi captive portal on ESP32
- LED displays slot status (available/registered/occupied)
- Automatic gate opening/closing via servo
- Real-time communication between ESP32 and server
- Sensors detect vehicles in slots
- Auto-cancel registration after 60 seconds if no check-in
- Check-in/check-out history logging

## System Architecture

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   React     │◄───────►│   Node.js    │◄───────►│   MySQL     │
│  Web Client │  HTTP   │   Server     │         │  Database   │
└─────────────┘         └──────────────┘         └─────────────┘
      │                        │
      │                        │ HTTP
      │                        ▼
      │                  ┌──────────────┐
      └─────────────────►│    ESP32     │
           Wi-Fi         │   + Sensors  │
        (Captive Portal) │   + Actuators│
                         └──────────────┘
```

## Hardware

| Device | Quantity | Function |
|--------|----------|----------|
| ESP32 Dev Module | 1 | Main microcontroller |
| HW-870 Sensor | 4 | Detects vehicles in slots |
| LED | 4 | Displays slot status |
| Servo SG90 | 1 | Controls barrier gate |
| Breadboard & Wires | - | Circuit connections |

### ESP32 Pin Diagram

| Function | GPIO Pin |
|-----------|-----------|
| Servo | D13 |
| LED Slot 1-4 | D26, D25, D33, D32 |
| Sensor Slot 1-4 | D18, D19, D21, D22 |

## Installation

### System Requirements

- Node.js >= 16.x
- Yarn >= 1.22
- MySQL >= 5.7
- Arduino IDE (for ESP32)
- ESP32 Board Support Package

### 1. Backend Server Setup

```bash
cd server
yarn install
```

Create `.env` file:

```env
DB_HOST=localhost
DB_USER=root
DB_PASSWORD=your_password
DB_NAME=autoparking
PORT=1204
```

Create MySQL database:

```sql
CREATE DATABASE autoparking;

USE autoparking;

CREATE TABLE slots (
  slot_number INT PRIMARY KEY,
  status VARCHAR(20) DEFAULT 'available',
  license_plate VARCHAR(20),
  otp VARCHAR(6)
);

CREATE TABLE logs (
  id INT AUTO_INCREMENT PRIMARY KEY,
  license_plate VARCHAR(20),
  otp VARCHAR(6),
  time_in DATETIME,
  time_out DATETIME
);

INSERT INTO slots (slot_number) VALUES (1), (2), (3), (4);
```

Run server:

```bash
yarn dev
```

### 2. Web Client Setup

```bash
cd web
yarn install
```

Create `.env` file:

```env
REACT_APP_API_BASE=http://localhost:1204
```

Run development:

```bash
yarn start
```

Build for production (to upload to ESP32):

```bash
yarn build
```

### 3. ESP32 Firmware Setup

1. Open Arduino IDE
2. Install ESP32 board support
3. Open file `arduino/esp32.ino`
4. Configure Wi-Fi in code (if needed)
5. Upload to ESP32
6. Upload file system (SPIFFS) with content from `arduino/data/`

## Usage

### Starting the System

1. **Start MySQL server**
2. **Run Node.js backend**:
   ```bash
   cd server && yarn dev
   ```
3. **Run React web client** (dev mode):
   ```bash
   cd web && yarn start
   ```
4. **Power on ESP32** - It will create Wi-Fi hotspot "Parking Hotspot" (password: `12042005`)

### Accessing the Interface

- **Web client (local)**: http://localhost:3000
- **Captive portal (ESP32)**: http://192.168.4.1
- **Backend API**: http://localhost:1204

### Usage Flow

1. **Register slot**: Select empty slot, enter license plate → receive OTP
2. **Check-in**: Upon arrival, enter license plate + OTP → gate opens automatically
3. **Park vehicle**: Vehicle enters slot, sensor detects → gate closes automatically
4. **Check-out**: Enter license plate + OTP → system checks slot is empty → opens gate

## API Documentation

### Endpoints

| Method | Endpoint | Description | Request Body |
|--------|----------|-------------|--------------|
| GET | `/slots` | Get all slots | - |
| POST | `/register` | Register new slot | `{ slot_number, license_plate }` |
| POST | `/checkin` | Check-in vehicle | `{ license_plate, otp }` |
| POST | `/checkout` | Check-out vehicle | `{ license_plate, otp }` |
| POST | `/status` | Check status | `{ license_plate, otp }` |
| GET | `/logs` | Get check-in/out history | - |
| GET | `/esp-slots` | API for ESP32 to get slot status | - |

### ESP32 Endpoints

| Method | Endpoint | Description | Request Body |
|--------|----------|-------------|--------------|
| POST | `/esp-checkin` | Open check-in gate | `{ slot }` |
| POST | `/esp-checkout` | Open check-out gate | `{ slot }` |
| POST | `/esp-led` | Update LED | `{ slot, status }` |
| GET | `/check-gate` | Check gate sensor | - |
| GET | `/slot-status?slot=X` | Check vehicle in slot | - |

### Response Format

**Success Response**:
```json
{
  "message": "Registered successfully",
  "otp": "123456"
}
```

**Error Response**:
```json
{
  "error": "Slot occupied"
}
```

## Project Structure

```
autoparking-server/
├── arduino/                  # ESP32 firmware
│   ├── esp32.ino            # Main firmware code
│   ├── data/                # SPIFFS data (React build)
│   └── tools/               # Upload tools
├── server/                  # Node.js backend
│   ├── server.js            # Main server file
│   ├── package.json         # Dependencies
│   └── .env                 # Environment config
├── web/                     # React frontend
│   ├── src/
│   │   ├── App.js           # Main component
│   │   └── App.css          # Styles
│   ├── public/              # Static assets
│   └── package.json         # Dependencies
├── document/                # Documentation & diagrams
│   ├── Circuit.png          # Circuit diagram
│   ├── Schema diagram.png   # System schema
│   └── IOT Report.pdf       # Project report
└── README.md                # This file
```

## Operation Flow

### 1. Slot Registration
```
User → Web Client → Backend → MySQL (save slot + OTP)
                             → ESP32 (update LED)
```

### 2. Check-in
```
User → Web Client → Backend → Verify OTP & License Plate
                             → Check gate sensor
                             → ESP32 (blink LED + open gate)
                             → Wait for car sensor
                             → Close gate + update LED
                             → MySQL (save time_in)
```

### 3. Check-out
```
User → Web Client → Backend → Verify check-in status
                             → Check slot sensor (must be empty)
                             → ESP32 (open gate)
                             → MySQL (save time_out, clear slot)
                             → Auto-close gate after 10s
```

### 4. Auto-cancel Registration
```
60 seconds after registration without check-in:
Backend → Check if time_in is NULL
        → Clear slot + delete log entry
        → ESP32 (reset LED)
```

## Development

### Debug ESP32
```bash
# Serial monitor
screen /dev/tty.usbserial-* 115200

# Or use Arduino IDE Serial Monitor
```

### Debug Backend
```bash
cd server
yarn dev  # Nodemon auto-reload
```

### Build Web Client for ESP32
```bash
cd web
yarn build
# Copy build/* to arduino/data/
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| ESP32 doesn't create Wi-Fi | Check flash memory, reset ESP32 |
| Captive portal doesn't activate | Access manually: `http://192.168.4.1` |
| Backend can't connect to DB | Check MySQL credentials in `.env` |
| ESP32 doesn't receive API calls | Ensure ESP32 and server are on same network |
| Servo not working | Check power supply and GPIO connections |

## Security Notes

- OTP is valid for 60 seconds
- License plate validation follows standard format
- Check sensors before opening gate
- Transaction rollback on database errors

## Future Improvements

- [ ] Add payment system
- [ ] Mobile app (React Native)
- [ ] Automatic license plate recognition camera
- [ ] Admin dashboard
- [ ] Push notifications for time expiry
- [ ] Multiple parking lots support
- [ ] Reservation system

## Author

**Khoa Nguyễn** (yunkhngn)
- GitHub: [@yunkhngn](https://github.com/yunkhngn)
- Email: yunkhngn.mail@gmail.com

## License

MIT License - Copyright © 2025 Khoa Nguyễn

## Contributing

All contributions are welcome! Please:
1. Fork the project
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

If you find this project useful, please give it a star!
