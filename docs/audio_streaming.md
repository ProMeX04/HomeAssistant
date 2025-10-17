# Truyền âm thanh thời gian thực từ ESP32 + INMP441 tới server

Tài liệu này mô tả một kiến trúc mẫu để đọc âm thanh từ microphone INMP441 qua giao tiếp I2S, gửi luồng PCM/Opus thời gian thực tới server để xử lý (ví dụ chuyển giọng nói thành văn bản và trả lời như một trợ lý ảo), đồng thời đảm bảo sử dụng ít RAM trên ESP32.

## 1. Kiến trúc tổng quan

1. **ESP32** đọc mẫu âm thanh 32-bit từ INMP441 bằng DMA của I2S, gộp thành các frame nhỏ (ví dụ 20–40 ms) để tránh buffer lớn.
2. **Công tắc vật lý** (push-to-talk) nối vào chân số hoá, khi người dùng nhấn mới kích hoạt việc đọc + gửi frame nhằm giảm âm nền và kiểm soát quyền riêng tư.
3. **Gửi UDP** tới server ngay khi có frame mới trong lúc công tắc đang bật. UDP cho phép độ trễ cực thấp, overhead nhỏ và không yêu cầu duy trì kết nối.
4. **Server** nhận packet UDP, ghép lại thành luồng theo `sequence`, `timestamp`, chạy pipeline chuyển giọng nói sang văn bản bằng Whisper và (tùy chọn) trả về phản hồi. Kênh phản hồi (văn bản/âm thanh) có thể sử dụng WebSocket hoặc HTTP riêng.

Hướng dẫn này tập trung vào UDP để tối ưu độ trễ và RAM. Mỗi packet gồm header 12 byte (sequence, timestamp, số mẫu, sample rate) + payload PCM 16-bit. Server dùng thông tin này để phát hiện packet mất hoặc trễ.

## 2. Cấu hình ESP32 (Arduino/PlatformIO)

File `src/main.cpp` đính kèm minh hoạ:

- Khởi tạo Wi-Fi STA.
- Cấu hình I2S ở chế độ `MASTER | RX`, tần số 16 kHz, mono.
- Mỗi lần đọc `256` mẫu (≈16 ms @16 kHz), chuyển đổi 32-bit đọc từ INMP441 sang 16-bit PCM rồi đóng gói header UDP (12 byte) + payload.
- Header gồm `sequence` (uint32), `timestamp_ms` (uint32), `sample_count` (uint16) và `sample_rate` (uint16) theo network-order (`htonl/htons`).
- Sử dụng `WiFiUDP` gửi packet ngay khi đọc xong **và chỉ khi công tắc push-to-talk đang bật**; nếu DNS chưa phân giải thành công thì thử lại định kỳ.
- Các hằng `kTriggerPin`, `kTriggerActiveLow` trong mã nguồn xác định chân công tắc và mức kích hoạt. Khi trạng thái chuyển sang bật, ESP32 sẽ xoá DMA buffer (`i2s_zero_dma_buffer`) và reset `sequence` để bắt đầu phiên mới.

> Lưu ý đổi `kWifiSsid`, `kWifiPassword`, địa chỉ server (`kServerHost`, `kServerPort`) và chân I2S cho phù hợp phần cứng.

### Giảm sử dụng RAM

- **DMA buffer nhỏ**: `dma_buf_len = 512` và chỉ dùng 4 buffer luân phiên (≈8 KB RAM).
- **Xử lý tại chỗ**: dùng chung buffer thô (`int32_t`) và buffer PCM (`int16_t`) với kích thước bằng nhau, chuyển đổi xong gửi ngay, không giữ lịch sử dài.
- **Không cấp phát động**: tất cả buffer khai báo tĩnh.

## 3. Lựa chọn nén/định dạng

| Định dạng | Ưu điểm | Nhược điểm | Ghi chú |
|-----------|---------|------------|--------|
| PCM 16-bit | Đơn giản, ít CPU | Băng thông cao (~512 byte/frame) | Dễ tích hợp với STT cloud |
| Opus | Nén mạnh, chất lượng cao | Cần thư viện ngoài, CPU cao hơn | Có thể dùng thư viện [libopus](https://github.com/espressif/esp-adf/tree/master/components/opus) hoặc ESP-ADF |
| Speex | Tối ưu giọng nói, nhẹ hơn Opus | Ít phổ biến hơn | Thích hợp 8–16 kHz |
| ADPCM | Nhẹ, có sẵn trong ESP-ADF | Chất lượng thấp hơn | Giảm băng thông 4:1 |

Nếu server đặt gần và băng thông Wi-Fi đủ, bắt đầu với PCM để đơn giản. Khi cần tối ưu băng thông, cân nhắc tích hợp Opus hoặc Speex:

- Chuyển mỗi frame PCM sang Opus (frame 20 ms @16 kHz → 320 mẫu) trước khi gửi.
- Gửi kèm header nhỏ chỉ ra codec, kích thước, timestamp.

## 4. Server mẫu (Python + Whisper trên Mac mini M4)

Thư mục `server/` chứa script `udp_receiver.py`. Phiên bản mới bổ sung:

- Gom các packet thành từng **phiên** dựa trên công tắc (khi không nhận thêm gói trong
  một khoảng thời gian cấu hình, phiên sẽ kết thúc).
- Tự động chạy Whisper để chuyển giọng nói sang văn bản (mặc định tiếng Việt) khi một
  phiên kết thúc và in kết quả ra terminal.
- Tùy chọn lưu mỗi phiên thành file WAV riêng (`--session-dir`) hoặc ghi toàn bộ luồng
  vào một file duy nhất (`--output`).

### Chuẩn bị môi trường trên macOS (Mac mini M4)

1. Cài `ffmpeg` – Whisper sử dụng để đọc/ghi WAV:

   ```bash
   brew install ffmpeg
   ```

2. Tạo môi trường ảo và cài thư viện (PyTorch hỗ trợ Metal/CPU trên Apple Silicon):

   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   pip install --upgrade pip
   pip install torch torchvision torchaudio
   pip install openai-whisper numpy
   ```

   > Nếu không dùng Metal có thể cài bản CPU: `pip install torch --index-url https://download.pytorch.org/whl/cpu`.

### Chạy server và nhận dạng tiếng Việt

Ví dụ cấu hình session timeout 1.2 giây và lưu các phiên vào thư mục `recordings`:

```bash
python server/udp_receiver.py \
  --host 0.0.0.0 --port 5000 \
  --session-dir recordings \
  --session-timeout 1.2 \
  --whisper-model small \
  --language vi
```

Các tuỳ chọn quan trọng:

- `--session-timeout`: khoảng im lặng (giây) để đánh dấu một phiên kết thúc.
- `--session-dir`: lưu file WAV cho từng phiên (tự tạo thư mục nếu chưa có).
- `--output`: ghi toàn bộ luồng vào một file WAV duy nhất (giống phiên bản trước).
- `--no-whisper`: tắt pipeline Whisper nếu chỉ muốn kiểm tra đường truyền.
- `--whisper-model`: chọn kích thước model (`tiny`, `small`, `medium`, `large-v3`, ...).
- `--language`: mã ngôn ngữ ISO cho Whisper, mặc định `vi` (tiếng Việt).

Ví dụ log khi công tắc bật/tắt:

```
▶️  Bắt đầu phiên mới từ 192.168.1.50:6000 (sample_rate=16000)
⏹️  Kết thúc phiên từ 192.168.1.50:6000 — Packets: 120, lost: 0, duration: 2.05s, rate: 58.4/s
📝  Đang nhận dạng 2.05s audio bằng Whisper model 'small' (ngôn ngữ=vi)
→ Văn bản: xin chào trợ lý, bật đèn phòng khách giúp tôi
```

## 5. Đồng bộ & chống mất gói

- Header UDP đã chứa `sequence` + `timestamp_ms` giúp phát hiện mất/đảo gói.
- Server gom từng phiên dựa trên thời gian im lặng (điều chỉnh bằng `--session-timeout`). Nếu cần phản hồi hoàn toàn thời gian thực, có thể tích hợp pipeline streaming (ví dụ Whisper streaming) thay vì chờ kết thúc phiên.
- Nếu cần nén, đóng gói codec (Opus, Speex) vào payload và báo kích thước trong header phụ.

## 6. Bảo mật & mở rộng

- Sử dụng WPA2-Enterprise hoặc mạng riêng cho Wi-Fi, và cân nhắc DTLS/SRTP nếu cần mã hoá UDP.
- Dùng kênh điều khiển (HTTP/WebSocket/MQTT) để server yêu cầu bắt đầu/kết thúc ghi, hoặc gửi phản hồi.
- Triển khai cơ chế VAD (Voice Activity Detection) trên server hoặc client để giảm lưu lượng khi im lặng.

## 7. Kiểm thử

1. Chạy server Python UDP ở trên.
2. Flash firmware ESP32 với mã trong `src/main.cpp`.
3. Mở Serial Monitor (`pio device monitor -b 115200`) kiểm tra log Wi-Fi/DNS.
4. Nói vào microphone, xác nhận log trên server hiển thị phiên mới + kết quả Whisper.

Sau khi pipeline push-to-talk + Whisper hoạt động ổn định, có thể bổ sung thêm lớp xử lý ngôn ngữ (LLM) và kênh phản hồi âm thanh/văn bản để hoàn thiện trợ lý ảo.
