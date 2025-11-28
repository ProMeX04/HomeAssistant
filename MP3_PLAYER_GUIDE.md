# ESP32 LyraT Mini - MP3 Player Mode

## Tổng quan
Firmware hỗ trợ 2 chế độ hoạt động:
1. **Wake Word Mode** (Mặc định): Nhận diện "Jarvis" và streaming audio qua WebSocket
2. **MP3 Player Mode**: Phát nhạc MP3 từ thẻ SD card

## Yêu cầu phần cứng
- ESP32-LyraT-Mini V1.2
- Thẻ SD card (formatted FAT32) có chứa file .mp3
- File MP3 đặt trong thư mục gốc của thẻ SD: `/sdcard/`

## Cấu hình SD Card SPI
```
GPIO15 -> MOSI (CMD)
GPIO2  -> MISO (SD0)
GPIO14 -> SCLK (CLK)
GPIO13 -> CS (Chip Select)
```

## Điều khiển

### Nút bấm
- **MODE**: Chuyển đổi giữa Wake Word ↔ MP3 Player
- **PLAY**: Play/Pause nhạc (chỉ trong MP3 mode)
- **REC**: Bài tiếp theo (Next track)
- **SET**: Bài trước đó (Previous track)
- **VOL+**: Tăng âm lượng
- **VOL-**: Giảm âm lượng

## Cách sử dụng

### 1. Chuẩn bị thẻ SD
```bash
# Format thẻ SD thành FAT32
# Copy file MP3 vào thẻ SD (thư mục gốc)
cp music/*.mp3 /Volumes/SDCARD/
```

### 2. Flash firmware
```bash
idf.py build flash monitor
```

### 3. Sử dụng

#### Wake Word Mode (Mặc định)
- Nói "Jarvis" để kích hoạt
- Hệ thống sẽ ghi âm và gửi lên server
- Nhận phản hồi audio từ server và phát

#### MP3 Player Mode
1. Nhấn nút **MODE** để chuyển sang MP3 mode
2. Nhạc sẽ tự động phát track đầu tiên (nếu có)
3. Sử dụng:
   - **PLAY**: Tạm dừng/Tiếp tục
   - **REC**: Chuyển bài tiếp theo
   - **SET**: Quay lại bài trước
4. Nhấn **MODE** lại để quay về Wake Word mode

## Tính năng MP3 Player

### Auto-scan playlist
- Tự động quét tất cả file .mp3 khi khởi động
- Hỗ trợ tối đa 100 file
- Hiển thị danh sách trong log

### Auto-next
- Tự động phát bài tiếp theo khi bài hiện tại kết thúc
- Loop lại từ đầu khi hết playlist

### Dynamic sample rate
- Tự động điều chỉnh I2S clock theo sample rate của file MP3
- Hỗ trợ: 44.1kHz, 48kHz, 32kHz, etc.

## Troubleshooting

### SD Card không mount
1. Kiểm tra thẻ SD đã format FAT32
2. Kiểm tra thẻ SD đã cắm đúng
3. Xem log: `SD Card mount failed: ...`
4. Thử thẻ SD khác

### Không có file MP3
```
W (xxx) LYRAT_MINI_WS: No MP3 files found on SD card
```
→ Copy file .mp3 vào thẻ SD

### WebSocket lỗi khi ở MP3 mode
→ Đã fix! WebSocket sẽ tự động stop khi chuyển sang MP3 mode

### Âm thanh kêu rè/méo
→ Kiểm tra format file MP3 (nên dùng 44.1kHz, 16-bit, stereo/mono)

## Log mẫu

### Khởi động thành công với SD card
```
I (xxx) LYRAT_MINI_WS: Initializing SD Card via SPI...
I (xxx) LYRAT_MINI_WS: ✅ SD Card mounted successfully
I (xxx) LYRAT_MINI_WS: SD Card: XXXXX, Size: XXXX MB
I (xxx) LYRAT_MINI_WS: Scanning SD card for MP3 files...
I (xxx) LYRAT_MINI_WS: [0] song1.mp3
I (xxx) LYRAT_MINI_WS: [1] song2.mp3
I (xxx) LYRAT_MINI_WS: Found 2 MP3 files
```

### Chuyển sang MP3 mode
```
I (xxx) LYRAT_MINI_WS: [ * ] [MODE] Click - Toggle Mode
I (xxx) LYRAT_MINI_WS: 🎵 Switching to MP3 Player Mode
I (xxx) LYRAT_MINI_WS: Stopping WebSocket client...
I (xxx) LYRAT_MINI_WS: Playing: /sdcard/song1.mp3
I (xxx) LYRAT_MINI_WS: MP3 Info: 44100 Hz, 2 ch, 16 bits
```

## Build Info
- Firmware size: ~1.2 MB
- Free partition space: ~54%
- RAM usage: Moderate (MP3 decoder + pipelines)
