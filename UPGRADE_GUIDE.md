# Hướng Dẫn Nâng Cấp Home Assistant

## 📋 Tổng Quan Cập Nhật

Dự án đã được nâng cấp với các tính năng mới:

1. **Giao diện mới giống ChatGPT** - Thiết kế hiện đại, tối giản
2. **Tính năng ghi âm** - Gửi lệnh điều khiển bằng giọng nói
3. **Tích hợp OpenAI Whisper** - Chuyển đổi giọng nói thành văn bản

## 🚀 Cài Đặt

### 1. Cài Đặt Dependencies

#### Server

```powershell
cd server
npm install openai multer
```

#### Client

Không cần cài đặt thêm dependencies mới.

### 2. Cấu Hình Môi Trường

Thêm OpenAI API key vào file `server/.env`:

```env
OPENAI_API_KEY=sk-your-openai-api-key-here
```

**Lấy API Key:**

1. Truy cập https://platform.openai.com/api-keys
2. Đăng nhập hoặc đăng ký tài khoản
3. Tạo API key mới
4. Sao chép và dán vào file `.env`

### 3. Tạo Thư Mục Uploads

Thư mục này đã được tạo tự động tại `server/uploads/`

## 🎨 Những Thay Đổi Giao Diện

### Màu Sắc Mới

- **Màu chủ đạo**: `#10a37f` (xanh lá ChatGPT)
- **Background**: Tối hơn, tối giản hơn
- **Chat bubbles**: Thiết kế giống ChatGPT

### Layout Mới

- **Sidebar**: Có thể thu gọn trên mobile
- **Chat area**: Toàn màn hình với scroll mượt mà
- **Input box**: Hiện đại với nút ghi âm và gửi

### Responsive Design

- Sidebar toggle button trên mobile
- Layout tự động điều chỉnh theo màn hình

## 🎤 Sử Dụng Tính Năng Ghi Âm

### Cách Sử Dụng

1. **Nhấn nút microphone** (biểu tượng mic) ở góc phải input box
2. **Cho phép truy cập microphone** khi trình duyệt yêu cầu
3. **Nói lệnh** của bạn (ví dụ: "Bật đèn phòng khách")
4. **Nhấn nút đỏ** (biểu tượng vuông) để dừng ghi âm
5. Hệ thống tự động:
   - Upload audio lên server
   - Chuyển đổi thành văn bản qua Whisper
   - Xử lý lệnh qua Gemini AI
   - Thực thi hành động

### Lưu Ý

- Chỉ hoạt động trên **HTTPS** hoặc **localhost**
- Cần có **microphone** khả dụng
- Giới hạn file audio: **25MB**
- Định dạng: WebM (tự động)

## 🔧 API Endpoints Mới

### POST `/api/commands/transcribe`

Chuyển đổi audio thành văn bản.

**Request:**

- Content-Type: `multipart/form-data`
- Body:
  - `audio`: File audio (WebM, MP3, WAV, etc.)
  - `language`: (optional) Mã ngôn ngữ (mặc định: 'vi')

**Response:**

```json
{
  "transcription": "Bật đèn phòng khách",
  "message": "Audio transcribed successfully"
}
```

## 📁 Cấu Trúc File Mới

```
server/
├── uploads/              # Thư mục lưu audio tạm thời (tự động xóa)
├── src/
│   ├── services/
│   │   └── whisperService.js   # Service xử lý Whisper API
│   ├── controllers/
│   │   └── commandController.js # Thêm transcribeAudioCommand
│   └── routes/
│       └── commandRoutes.js    # Thêm route /transcribe

client/
├── src/
│   ├── App.jsx          # Thêm handleAudioSubmit
│   ├── App.css          # Redesign ChatGPT style
│   └── components/
│       └── NaturalLanguageForm.jsx  # Thêm tính năng ghi âm
```

## 🧪 Testing

### Test Ghi Âm

1. Mở ứng dụng trên trình duyệt
2. Nhấn nút microphone
3. Nói: "Bật đèn phòng khách"
4. Dừng ghi âm
5. Kiểm tra log để xem kết quả

### Test API Trực Tiếp

```powershell
# Gửi file audio test
curl -X POST http://localhost:5000/api/commands/transcribe `
  -F "audio=@test-audio.webm" `
  -F "language=vi"
```

## 🐛 Xử Lý Lỗi Thường Gặp

### Lỗi: "OpenAI API key is not configured"

**Giải pháp:** Kiểm tra file `.env` có chứa `OPENAI_API_KEY`

### Lỗi: "Cannot access microphone"

**Giải pháp:**

- Kiểm tra quyền microphone trong trình duyệt
- Đảm bảo đang chạy trên HTTPS hoặc localhost

### Lỗi: "Only audio files are allowed"

**Giải pháp:** Đảm bảo file upload có MIME type là audio/\*

### Lỗi: File quá lớn

**Giải pháp:** Giới hạn file là 25MB, ghi âm ngắn hơn

## 💰 Chi Phí Sử Dụng

### OpenAI Whisper API

- **$0.006** / phút audio
- Ví dụ: 10 phút = $0.06
- Xem thêm: https://openai.com/api/pricing/

### Gemini API

- Free tier: 1500 requests/day
- Xem thêm: https://ai.google.dev/pricing

## 🔐 Bảo Mật

1. **Không commit** file `.env` lên git
2. **Giới hạn** file size để tránh abuse
3. **Xóa** file audio sau khi xử lý
4. **Validate** input trước khi xử lý

## 📚 Tài Liệu Tham Khảo

- [OpenAI Whisper API](https://platform.openai.com/docs/guides/speech-to-text)
- [MediaRecorder API](https://developer.mozilla.org/en-US/docs/Web/API/MediaRecorder)
- [Multer Documentation](https://github.com/expressjs/multer)

## 🤝 Hỗ Trợ

Nếu gặp vấn đề, vui lòng:

1. Kiểm tra console logs
2. Xem file `server/logs/` (nếu có)
3. Đảm bảo tất cả dependencies đã được cài đặt
4. Kiểm tra API keys hợp lệ
