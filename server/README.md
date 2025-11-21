# 🚗 Hệ thống AutoParking IoT

## ℹ️ Giới thiệu
Đây là một hệ thống đỗ xe thông minh mô phỏng sử dụng ESP32, cảm biến, LED, servo và một hệ thống web để đăng ký, check-in và check-out bãi đỗ xe.

## ✨ Tính năng
- Đăng ký slot đỗ xe qua website (chạy trên ESP32 captive portal hoặc website chạy trên máy chủ nội bộ)
- Check-in, check-out xe bằng biển số và mã OTP
- Giao tiếp ESP32 với server qua API nội bộ
- Tự động mở/đóng cổng bằng servo
- Cảnh báo đèn nhấp nháy khi xe sắp vào slot
- LED hiển thị trạng thái slot (trống / đã đăng ký / đang chờ xe vào)

## 🧰 Phần cứng

| Thiết bị           | Mô tả                                  |
|--------------------|-----------------------------------------|
| ESP32 Dev Module   | Vi điều khiển chính                     |
| 4 cảm biến HW-870  | Phát hiện xe trong slot                 |
| 4 đèn LED          | Hiển thị trạng thái từng slot           |
| 1 Servo SG90       | Điều khiển cổng                         |
| Dây nối, breadboard| Kết nối mạch                            |

## 🔌 Giao tiếp chân (ESP32)
- Servo: D13
- LED: D26, D25, D33, D32 (slot 1 → 4)
- Cảm biến: D18, D19, D21, D22 (slot 1 → 4)

## 🖥️ Backend (Node.js + MySQL)
- Địa chỉ: `http://localhost:1204`
- Cơ sở dữ liệu: MySQL, gồm bảng `slots` và `logs`
- API chính:

| Endpoint         | Chức năng                                 |
|------------------|--------------------------------------------|
| `GET /slots`     | Lấy danh sách slot                         |
| `POST /register` | Đăng ký slot và sinh OTP                   |
| `POST /checkin`  | Check-in xe                                |
| `POST /checkout` | Check-out xe                               |
| `POST /status`   | Kiểm tra trạng thái check-in/out           |
| `GET /logs`      | Lấy lịch sử check-in/out                   |

## 🌐 Client (React)
- Giao diện web đơn giản: chọn slot, nhập biển số, thao tác check-in/out
- Gửi và nhận dữ liệu qua API nội bộ

## 📡 ESP32 - Web server captive portal
- Phát Wi-Fi "Parking Hotspot" (password: `12042005`)
- Lưu và phục vụ website React build (dưới dạng SPIFFS)
- Các endpoint nhận dữ liệu từ server:
  - `POST /esp-checkin { slot }`
  - `POST /esp-checkout { slot }`

## 🔄 Quy trình hoạt động
1. Người dùng truy cập vào Wi-Fi ESP32 hoặc đăng ký trên web
2. Đăng ký biển số và slot đỗ → nhận OTP
3. Khi tới bến đỗ, người dùng nhập biển số và OTP để check-in
4. ESP sẽ:
   - Nháy đèn slot đang chờ
   - Mở cổng sau 3 giây
   - Khi cảm biến phát hiện xe đã vào, tắt đèn và đóng cổng
5. Khi người dùng check-out:
   - Nếu cảm biến xác nhận slot trống → mở cổng
   - Sau 10s → đóng cổng

## 📝 Ghi chú
- Server phải nằm trong cùng mạng với ESP32
- Web React build phải được convert sang file tĩnh và upload lên SPIFFS
- Nếu không popup captive portal, truy cập thủ công: `http://192.168.4.1`

## 👤 Tác giả
Khoa Nguyễn (yunkhngn)

---
© 2025 yunkhngn
