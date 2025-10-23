# 🏠 Home Assistant v2.0 - ChatGPT Style

> **Hệ thống điều khiển nhà thông minh với giao diện hiện đại và điều khiển bằng giọng nói**

![Status](https://img.shields.io/badge/status-ready-success)
![Version](https://img.shields.io/badge/version-2.0.0-blue)
![License](https://img.shields.io/badge/license-MIT-green)

## ✨ Tính Năng Nổi Bật

### 🎨 Giao Diện Mới

- ✅ **Design giống ChatGPT** - Giao diện tối giản, hiện đại
- ✅ **Dark Theme** - Màu tối chuyên nghiệp (#10a37f accent)
- ✅ **Fully Responsive** - Hoạt động tốt trên mọi thiết bị
- ✅ **Smooth Animations** - Hiệu ứng chuyển động mượt mà

### 🎤 Điều Khiển Giọng Nói

- ✅ **Speech-to-Text** - Tích hợp OpenAI Whisper
- ✅ **Multi-language** - Hỗ trợ tiếng Việt và Anh
- ✅ **Real-time Recording** - Ghi âm với hiệu ứng trực quan
- ✅ **Auto Processing** - Tự động xử lý và thực thi lệnh

### 🤖 AI-Powered

- ✅ **Natural Language** - Hiểu ngôn ngữ tự nhiên qua Gemini AI
- ✅ **Smart Scheduling** - Tự động lên lịch từ lệnh
- ✅ **Context Aware** - Nhận diện thiết bị và hành động

### 🏠 Quản Lý Thiết Bị

- ✅ **Real-time Status** - Trạng thái thiết bị real-time
- ✅ **Quick Actions** - Điều khiển nhanh ON/OFF/Toggle
- ✅ **Device Management** - Thêm, sửa, xóa thiết bị
- ✅ **MQTT Integration** - Kết nối ESP32 qua MQTT

## 🚀 Bắt Đầu Nhanh

### 📋 Yêu Cầu

- Node.js >= 16.x
- MongoDB
- MQTT Broker (Mosquitto)
- Gemini API Key
- OpenAI API Key

### ⚡ Cài Đặt Express

```powershell
# 1. Clone repository (nếu cần)
git clone <your-repo-url>
cd HomeAssistant

# 2. Cài đặt dependencies
cd server && npm install
cd ../client && npm install

# 3. Cấu hình environment
# Tạo file server/.env với nội dung:
PORT=5000
MONGODB_URI=mongodb://127.0.0.1:27017/homeassistant
MQTT_URL=mqtt://127.0.0.1:1883
GEMINI_API_KEY=your-gemini-key
OPENAI_API_KEY=your-openai-key

# 4. Chạy ứng dụng
# Terminal 1 - Server
cd server && npm run dev

# Terminal 2 - Client
cd client && npm run dev

# 5. Truy cập
# Mở http://localhost:5173
```

### 🔑 Lấy API Keys

**Gemini API:**

1. Truy cập: https://aistudio.google.com/app/apikey
2. Đăng nhập với Google Account
3. Tạo API key mới
4. Copy vào `.env`

**OpenAI API:**

1. Truy cập: https://platform.openai.com/api-keys
2. Đăng ký/đăng nhập
3. Tạo API key
4. Copy vào `.env`

## 📖 Documentation

| Document                               | Description                    |
| -------------------------------------- | ------------------------------ |
| [QUICKSTART.md](./QUICKSTART.md)       | Hướng dẫn bắt đầu nhanh 5 phút |
| [UPGRADE_GUIDE.md](./UPGRADE_GUIDE.md) | Hướng dẫn nâng cấp chi tiết    |
| [DESIGN_GUIDE.md](./DESIGN_GUIDE.md)   | Tài liệu thiết kế giao diện    |
| [CHANGELOG.md](./CHANGELOG.md)         | Lịch sử thay đổi               |
| [client/README.md](./client/README.md) | Documentation client           |

## 🎮 Hướng Dẫn Sử Dụng

### 💬 Gửi Lệnh Văn Bản

```
Ví dụ:
"Bật đèn phòng khách"
"Tắt quạt phòng ngủ"
"Đặt nhiệt độ máy lạnh 25 độ"
"Bật đèn phòng khách lúc 7 giờ tối"
```

### 🎤 Điều Khiển Giọng Nói

1. Nhấn nút **microphone** (🎤)
2. Cho phép truy cập microphone
3. Nói lệnh: _"Bật đèn phòng khách"_
4. Nhấn nút **dừng** (■)
5. Hệ thống tự động xử lý

### 🏠 Điều Khiển Trực Tiếp

1. Mở **sidebar** (☰)
2. Chọn thiết bị
3. Nhấn **ON/OFF/Toggle**

## 🏗️ Kiến Trúc

```
┌─────────────────────────────────────────────────────────┐
│                     CLIENT (React)                      │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐ │
│  │   Sidebar   │  │  Chat Area   │  │  Input Box    │ │
│  │  - Devices  │  │  - Messages  │  │  - Text       │ │
│  │  - Schedule │  │  - Loading   │  │  - Voice 🎤   │ │
│  └─────────────┘  └──────────────┘  └───────────────┘ │
└────────────────────────┬────────────────────────────────┘
                         │ HTTP/WebSocket
┌────────────────────────┴────────────────────────────────┐
│                  SERVER (Express.js)                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │   Routes     │  │ Controllers  │  │   Services   │ │
│  │  /commands   │→ │  command     │→ │  - Whisper   │ │
│  │  /devices    │  │  device      │  │  - Gemini    │ │
│  │  /schedules  │  │  schedule    │  │  - MQTT      │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
└────────────┬────────────────┬──────────────────────────┘
             │                │
    ┌────────┴────────┐  ┌───┴──────┐
    │    MongoDB      │  │   MQTT   │
    │   (Database)    │  │ (Broker) │
    └─────────────────┘  └────┬─────┘
                              │
                      ┌───────┴────────┐
                      │  ESP32 Devices │
                      │  - Sensors     │
                      │  - Actuators   │
                      └────────────────┘
```

## 🛠️ Tech Stack

### Frontend

- **React 18** - UI Framework
- **Vite** - Build Tool
- **Axios** - HTTP Client
- **Day.js** - Date Handling

### Backend

- **Express.js** - Web Framework
- **MongoDB** - Database
- **Mongoose** - ODM
- **MQTT.js** - MQTT Client
- **Multer** - File Upload

### AI/ML

- **Google Gemini** - Natural Language Understanding
- **OpenAI Whisper** - Speech-to-Text

### IoT

- **MQTT** - Message Protocol
- **ESP32** - Microcontroller

## 📁 Cấu Trúc Project

```
HomeAssistant/
├── client/                 # React Frontend
│   ├── src/
│   │   ├── components/     # React Components
│   │   │   ├── CommandLog.jsx
│   │   │   ├── DeviceList.jsx
│   │   │   ├── NaturalLanguageForm.jsx
│   │   │   └── ScheduleList.jsx
│   │   ├── api/           # API Client
│   │   ├── App.jsx        # Main App
│   │   └── App.css        # Styles
│   └── package.json
│
├── server/                # Express Backend
│   ├── src/
│   │   ├── controllers/   # Request Handlers
│   │   ├── models/        # MongoDB Models
│   │   ├── routes/        # API Routes
│   │   ├── services/      # Business Logic
│   │   │   ├── whisperService.js
│   │   │   ├── geminiService.js
│   │   │   ├── mqttService.js
│   │   │   └── commandService.js
│   │   └── app.js
│   ├── uploads/           # Temp Audio Files
│   └── package.json
│
├── src/                   # ESP32 Firmware
│   └── main.cpp
│
├── QUICKSTART.md          # Quick Start Guide
├── UPGRADE_GUIDE.md       # Upgrade Guide
├── DESIGN_GUIDE.md        # Design Documentation
├── CHANGELOG.md           # Change Log
└── README.md              # This file
```

## 🎨 Screenshots

### Desktop View

```
┌─────────────────────────────────────────────────────────┐
│  🏠 Home Assistant                                       │
├──────────┬──────────────────────────────────────────────┤
│ Sidebar  │            Chat Messages                     │
│          │  ┌────────────────────┐                      │
│ 💡 Đèn   │  │ Bạn: Bật đèn      │ →                    │
│ 🌡️ Nhiệt │  └────────────────────┘                      │
│ 🔌 Ổ cắm │                                               │
│          │  ← ┌────────────────────┐                    │
│ 📅 Lịch  │    │ AI: Đã bật đèn    │                    │
│          │    └────────────────────┘                    │
└──────────┴───────────────────────────────────────────────┤
           │  [Gửi tin nhắn...] [🎤] [✈️]                 │
           └──────────────────────────────────────────────┘
```

### Mobile View

```
┌────────────────────────────┐
│ ☰ Home Assistant           │
├────────────────────────────┤
│                            │
│  ┌──────────────────┐      │
│  │ Bạn: Bật đèn    │ →    │
│  └──────────────────┘      │
│                            │
│  ← ┌──────────────────┐    │
│    │ AI: Đã bật đèn  │    │
│    └──────────────────┘    │
│                            │
├────────────────────────────┤
│ [Message...] [🎤] [✈️]    │
└────────────────────────────┘
```

## 🧪 Testing

### Manual Testing

```powershell
# Test server health
curl http://localhost:5000/api/health

# Test transcribe endpoint
curl -X POST http://localhost:5000/api/commands/transcribe `
  -F "audio=@test-audio.webm"

# Test natural language
curl -X POST http://localhost:5000/api/commands/natural `
  -H "Content-Type: application/json" `
  -d "{\"prompt\":\"Bật đèn phòng khách\"}"
```

## 🐛 Troubleshooting

### Common Issues

**❌ Server không khởi động**

```powershell
# Check port conflict
netstat -ano | findstr :5000
# Kill process
taskkill /PID <pid> /F
```

**❌ Microphone không hoạt động**

- Chỉ hoạt động trên HTTPS hoặc localhost
- Kiểm tra quyền trong browser settings
- Sử dụng Chrome/Edge (recommended)

**❌ MongoDB connection failed**

```powershell
# Start MongoDB service
net start MongoDB
```

**❌ MQTT connection failed**

```powershell
# Start Mosquitto
net start mosquitto
```

## 💰 Cost Estimate

### OpenAI Whisper

- **$0.006** per minute of audio
- Example: 100 minutes/month = **$0.60**

### Google Gemini

- **Free**: 1500 requests/day
- **Paid**: $0.00025 per request

### Total Estimated Cost

- Light usage (< 100 commands/day): **< $5/month**
- Medium usage (< 500 commands/day): **< $20/month**

## 🔐 Security

- ✅ API keys in environment variables
- ✅ File upload validation
- ✅ Size limits (25MB)
- ✅ Auto cleanup temp files
- ✅ CORS configuration
- ✅ Input sanitization

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository
2. Create feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit changes (`git commit -m 'Add AmazingFeature'`)
4. Push to branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License.

## 🙏 Acknowledgments

- [OpenAI Whisper](https://openai.com/research/whisper) - Speech recognition
- [Google Gemini](https://deepmind.google/technologies/gemini/) - Natural language understanding
- [ChatGPT](https://chat.openai.com) - UI inspiration
- [React](https://react.dev) - Frontend framework
- [Express.js](https://expressjs.com) - Backend framework

## 📞 Support

Nếu gặp vấn đề:

1. Xem [QUICKSTART.md](./QUICKSTART.md)
2. Kiểm tra [Troubleshooting](#-troubleshooting)
3. Đọc [UPGRADE_GUIDE.md](./UPGRADE_GUIDE.md)
4. Tạo Issue trên GitHub

## 🗺️ Roadmap

- [ ] User authentication
- [ ] Multi-user support
- [ ] Voice synthesis (Text-to-Speech)
- [ ] Mobile app (React Native)
- [ ] Docker deployment
- [ ] Home automation scenes
- [ ] Energy monitoring
- [ ] Weather integration

---

**Made with ❤️ using GitHub Copilot**

**Version**: 2.0.0 | **Last Updated**: October 23, 2025
