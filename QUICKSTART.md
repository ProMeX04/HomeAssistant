# 🚀 Quick Start Guide

## Bắt Đầu Nhanh trong 5 Phút

### 1️⃣ Cài Đặt Dependencies

```powershell
# Server
cd server
npm install

# Client
cd ../client
npm install
```

### 2️⃣ Cấu Hình

Tạo file `server/.env`:

```env
PORT=5000
MONGODB_URI=mongodb://127.0.0.1:27017/homeassistant
MQTT_URL=mqtt://127.0.0.1:1883
GEMINI_API_KEY=your-gemini-key
OPENAI_API_KEY=your-openai-key
```

**Lấy API Keys:**

- Gemini: https://aistudio.google.com/app/apikey
- OpenAI: https://platform.openai.com/api-keys

### 3️⃣ Chạy Ứng Dụng

**Terminal 1 - Server:**

```powershell
cd server
npm run dev
```

**Terminal 2 - Client:**

```powershell
cd client
npm run dev
```

### 4️⃣ Truy Cập

Mở trình duyệt: **http://localhost:5173**

---

## 🎤 Sử Dụng Ghi Âm

1. Nhấn nút **microphone** (🎤)
2. Cho phép truy cập microphone
3. Nói: _"Bật đèn phòng khách"_
4. Nhấn nút **dừng** (■)
5. Đợi xử lý → Xem kết quả

---

## 💬 Gửi Tin Nhắn

1. Gõ tin nhắn vào ô input
2. Nhấn **Enter** hoặc nút gửi (✈️)
3. Xem phản hồi trong chat

---

## 🏠 Điều Khiển Thiết Bị

### Qua Chat:

```
"Bật đèn phòng khách"
"Tắt quạt phòng ngủ"
"Đặt nhiệt độ 25 độ"
```

### Qua Sidebar:

1. Mở sidebar (nếu đang thu gọn)
2. Chọn thiết bị
3. Nhấn ON/OFF/Toggle

---

## ⚙️ Cài Đặt Services (Tùy Chọn)

### MongoDB

```powershell
# Windows (với Chocolatey)
choco install mongodb

# Hoặc download: https://www.mongodb.com/try/download/community
```

### MQTT Broker (Mosquitto)

```powershell
# Windows
choco install mosquitto

# Hoặc download: https://mosquitto.org/download/
```

---

## 🔍 Kiểm Tra

### Test Server

```powershell
curl http://localhost:5000/api/health
# Response: {"status":"ok"}
```

### Test Transcribe

```powershell
curl -X POST http://localhost:5000/api/commands/transcribe `
  -F "audio=@test.webm"
```

---

## 🆘 Troubleshooting

### Server không chạy

```powershell
# Kiểm tra port
netstat -ano | findstr :5000

# Kill process nếu cần
taskkill /PID <process-id> /F
```

### Client không build

```powershell
# Clear cache
npm cache clean --force
rm -rf node_modules
npm install
```

### Microphone không hoạt động

- Đảm bảo đang dùng Chrome/Edge
- Chạy trên localhost hoặc HTTPS
- Kiểm tra quyền trong Settings

---

## 📚 Xem Thêm

- [UPGRADE_GUIDE.md](./UPGRADE_GUIDE.md) - Hướng dẫn chi tiết
- [DESIGN_GUIDE.md](./DESIGN_GUIDE.md) - Tài liệu thiết kế
- [CHANGELOG.md](./CHANGELOG.md) - Lịch sử thay đổi

---

**Happy Coding! 🎉**
