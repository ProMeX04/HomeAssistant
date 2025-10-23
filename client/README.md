# Home Assistant - Client

Giao diện web hiện đại cho hệ thống Home Assistant, được thiết kế theo phong cách ChatGPT.

## ✨ Tính Năng

### 🎨 Giao Diện

- **Design giống ChatGPT**: Giao diện tối giản, hiện đại
- **Dark Theme**: Màu tối chuyên nghiệp
- **Responsive**: Tự động điều chỉnh theo màn hình
- **Smooth Animations**: Hiệu ứng mượt mà

### 💬 Chat Interface

- **Real-time messaging**: Gửi và nhận lệnh ngay lập tức
- **Command history**: Xem lịch sử các lệnh đã gửi
- **Auto-scroll**: Tự động cuộn xuống tin nhắn mới
- **Loading indicators**: Hiển thị trạng thái đang xử lý

### 🎤 Voice Control

- **Speech-to-text**: Ghi âm và chuyển đổi thành văn bản
- **Real-time recording**: Hiển thị thời gian ghi âm
- **Visual feedback**: Hiệu ứng khi đang ghi âm
- **Easy controls**: Dễ dàng bắt đầu/dừng ghi âm

### 🏠 Device Management

- **Device list**: Danh sách thiết bị trong sidebar
- **Quick actions**: Điều khiển nhanh thiết bị
- **Device status**: Hiển thị trạng thái real-time
- **Device editing**: Chỉnh sửa thông tin thiết bị

### 📅 Schedule Management

- **View schedules**: Xem lịch hẹn
- **Status indicators**: Trạng thái pending/completed
- **Time display**: Hiển thị thời gian rõ ràng

## 🚀 Chạy Ứng Dụng

### Development Mode

```bash
npm run dev
```

Mở http://localhost:5173 để xem ứng dụng.

### Production Build

```bash
npm run build
```

### Preview Production Build

```bash
npm run preview
```

## 🎮 Sử Dụng

### Gửi Lệnh Văn Bản

1. Nhập lệnh vào ô text area
2. Nhấn Enter hoặc nút gửi (biểu tượng máy bay)
3. Xem kết quả trong chat log

**Ví dụ lệnh:**

- "Bật đèn phòng khách"
- "Tắt quạt phòng ngủ"
- "Đặt nhiệt độ máy lạnh 25 độ"
- "Bật đèn phòng khách lúc 7 giờ tối"

### Gửi Lệnh Bằng Giọng Nói

1. Nhấn nút microphone (biểu tượng mic)
2. Cho phép trình duyệt truy cập microphone
3. Nói lệnh của bạn
4. Nhấn nút dừng (hình vuông đỏ)
5. Chờ hệ thống xử lý

**Tips:**

- Nói rõ ràng, từ tốc độ vừa phải
- Tránh tiếng ồn xung quanh
- Microphone chỉ hoạt động trên HTTPS hoặc localhost

### Điều Khiển Thiết Bị Trực Tiếp

1. Mở sidebar (nếu đang thu gọn)
2. Tìm thiết bị cần điều khiển
3. Nhấn các nút quick action (ON/OFF/Toggle)

### Chỉnh Sửa Thiết Bị

1. Nhấn nút "Edit" trên device card
2. Thay đổi thông tin (name, type, location)
3. Nhấn "Save" để lưu

## 🎨 Customization

### Thay Đổi Màu Sắc

Chỉnh sửa CSS variables trong `src/App.css`:

```css
:root {
  --bg: #0c0c0f; /* Màu nền chính */
  --bg-chat: #212121; /* Màu nền chat */
  --accent: #10a37f; /* Màu accent (xanh lá) */
  --text-primary: #ececec; /* Màu chữ chính */
  --text-secondary: #9b9b9b; /* Màu chữ phụ */
}
```

### Thay Đổi Font

```css
:root {
  font-family: "Your-Font", system-ui, sans-serif;
}
```

## 📦 Dependencies

### Production

- **react**: ^18.3.1
- **react-dom**: ^18.3.1
- **axios**: ^1.7.2 - HTTP client
- **dayjs**: ^1.11.11 - Date formatting
- **prop-types**: ^15.8.1 - Type checking

### Development

- **vite**: ^5.3.4 - Build tool
- **@vitejs/plugin-react**: ^4.2.1 - React plugin for Vite

## 🌐 Browser Support

- Chrome/Edge: ✅ (Recommended)
- Firefox: ✅
- Safari: ✅
- Opera: ✅

**Note:** Tính năng ghi âm yêu cầu:

- HTTPS hoặc localhost
- Browser hỗ trợ MediaRecorder API
- Quyền truy cập microphone

## 📱 Responsive Breakpoints

- **Desktop**: > 1024px - Full layout với sidebar
- **Tablet**: 768px - 1024px - Sidebar toggleable
- **Mobile**: < 768px - Compact layout

## 🔧 Troubleshooting

### Microphone không hoạt động

1. Kiểm tra quyền truy cập trong browser settings
2. Đảm bảo đang chạy trên HTTPS hoặc localhost
3. Thử refresh trang và cho phép lại

### Không kết nối được server

1. Kiểm tra server đang chạy (port 5000)
2. Kiểm tra CORS configuration
3. Xem console logs để debug

### Giao diện bị lỗi

1. Clear browser cache
2. Hard refresh (Ctrl + Shift + R)
3. Rebuild project: `npm run build`

## 🎯 Performance Tips

- Sử dụng production build cho deployment
- Enable gzip compression trên server
- Lazy load images nếu có
- Minimize re-renders với React.memo

## 📄 License

MIT
