# Hệ thống giám sát và điều khiển ESP32 qua trợ lý chat

Tài liệu này mô tả kiến trúc mới thay thế cho giải pháp truyền âm thanh cũ. ESP32 hiện đóng vai trò thiết bị thu thập dữ liệu cảm biến (độ ẩm, ánh sáng) và nhận lệnh điều khiển thông qua máy chủ FastAPI. Người vận hành tương tác bằng giao diện web/chat, backend sử dụng Gemini để hiểu ngôn ngữ tự nhiên, đồng thời ghi log vào cơ sở dữ liệu SQLite.

## 1. Luồng tổng quan

1. **Giao diện web** (`/ui/index.html`) cho phép người dùng nhập lệnh tiếng Việt. Lịch sử hội thoại hiển thị song song với số liệu cảm biến mới nhất và danh sách lệnh đã lập lịch.
2. **Backend FastAPI** (`server/app.py`) nhận yêu cầu từ UI, gọi Gemini (nếu có khóa `GEMINI_API_KEY`) để phân tích lệnh → trích xuất hành động (`set_led`, `set_collection`, hoặc lập lịch tắt thiết bị). Backend lưu log vào SQLite (`data/homeassistant.db`) và xếp lệnh chờ thực thi.
3. **ESP32** (`src/main.cpp`) định kỳ publish dữ liệu cảm biến lên topic MQTT `homeassistant/<device_id>/telemetry` và lắng nghe lệnh tại `homeassistant/<device_id>/command`. Khi nhận được lệnh phù hợp, thiết bị điều khiển đèn D2 hoặc bật/tắt việc thu thập dữ liệu.
4. **Tác vụ lập lịch**: khi người dùng yêu cầu "tắt đèn sau 30 phút", backend lưu lệnh với `execute_at` tương ứng. ESP32 chỉ nhận được lệnh khi thời điểm này đã đến, đảm bảo thực thi đúng lịch.

## 2. ESP32 firmware

- Wi-Fi STA kết nối tới mạng cấu hình trong `kWifiSsid`/`kWifiPassword`.
- Đọc cảm biến analog qua `GPIO34` (độ ẩm) và `GPIO35` (ánh sáng), chuyển đổi thang 0–4095 thành phần trăm.
- Mỗi 10 giây publish JSON dạng `{"humidity": 54.2, "light": 20.5}` lên broker MQTT.
- Lắng nghe topic lệnh và phản hồi ngay khi nhận được tin nhắn. Lệnh hỗ trợ:
  - `set_led` với `state` = `on`/`off` (điều khiển D2).
  - `set_collection` với `state` = `on`/`off` (bật/tắt gửi dữ liệu cảm biến).
- Audio INMP441/I2S đã được loại bỏ hoàn toàn.

## 3. Backend FastAPI + SQLite

- Chạy bằng `uvicorn server.app:app --reload` (cài đặt phụ thuộc trong `server/requirements.txt`).
- Endpoint chính:
  - `POST /api/chat`: nhận tin nhắn người dùng, gọi Gemini, tạo lệnh/schedule.
  - `GET /api/devices/{device_id}/commands`: danh sách lệnh pending/done.
  - `GET /api/devices/{device_id}/sensors/recent`: trả dữ liệu cảm biến gần nhất cho UI.
- Bridge MQTT (`server/mqtt_manager.py`) chạy song song với FastAPI:
  - Đăng ký topic `homeassistant/+/telemetry` để lưu dữ liệu vào SQLite.
  - Gửi lệnh đến topic `homeassistant/<device_id>/command` khi lệnh đến hạn (bao gồm cả lập lịch tắt đèn).
- CSDL (`server/storage.py`) tự tạo bảng khi khởi động: `device_sensor_logs`, `device_actions`, `pending_commands`.
- Tích hợp Gemini (`server/gemini_client.py`):
  - Nếu có API key → gọi model `gemini-pro` và buộc trả JSON (`reply`, `command`, `delay_seconds`, `parameters`).
  - Nếu không có → dùng heuristic tiếng Việt ("bật", "tắt", "sau X phút", "dừng", "tiếp tục").

## 4. Giao diện web

- Tập tin tĩnh trong `server/static/` (dùng route `/ui`).
- `app.js` gọi API chat, cập nhật lịch lệnh và số liệu cảm biến, hiển thị log hội thoại.
- `styles.css` tạo theme nền tối và bố cục hai cột.

## 5. Quy trình vận hành

1. Khởi chạy broker MQTT (ví dụ `mosquitto`) ở địa chỉ cố định.
2. Thiết lập biến môi trường `MQTT_HOST`, `MQTT_PORT` (nếu khác mặc định) trước khi chạy backend: `uvicorn server.app:app --host 0.0.0.0 --port 8000`.
3. Mở trình duyệt tới `http://<server>:8000/ui/` để sử dụng giao diện chat.
4. ESP32 flash firmware mới, cấu hình `kMqttHost` và `kMqttPort` khớp với broker.
5. Theo dõi log backend/Serial để kiểm tra dữ liệu cảm biến và lệnh.
6. Khi cần lập lịch tắt thiết bị: nhập "tắt đèn sau 30 phút" → backend lưu lệnh, bridge MQTT gửi tín hiệu đúng thời điểm, ESP32 tắt D2 ngay khi nhận được.

## 6. Ghi chú bảo mật & mở rộng

- Cần thêm cơ chế xác thực giữa ESP32 và backend (token, HTTPS) trước khi đưa vào sản xuất.
- Có thể mở rộng bảng log để lưu thêm nhiệt độ, trạng thái thiết bị khác hoặc lịch sử hội thoại.
- Scheduler không còn phụ thuộc vào ESP32 polling; bridge MQTT gửi lệnh ngay khi đến hạn, có thể mở rộng thêm QoS hoặc retain tùy nhu cầu.
