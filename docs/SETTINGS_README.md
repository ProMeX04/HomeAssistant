# ESP32 Persistent Settings Module

## Tổng quan

Module này cung cấp chức năng lưu trữ cài đặt ứng dụng vào bộ nhớ dài hạn (NVS - Non-Volatile Storage) trên ESP32. Các cài đặt sẽ được lưu giữ ngay cả khi thiết bị tắt nguồn hoặc khởi động lại.

## Tính năng

### Cài đặt được hỗ trợ:

1. **Volume (Âm lượng)**: 0-100
   - Được lưu tự động khi thay đổi bằng nút Vol+/Vol-
   - Khôi phục tự động khi khởi động

2. **Microphone Gain**: -10 đến +10
   - Dự phòng cho tương lai

3. **Auto Wake**: true/false
   - Dự phòng cho tương lai

## Cách sử dụng

### 1. Cài đặt ban đầu

Khi ESP32 khởi động lần đầu tiên (hoặc sau khi xóa NVS), các giá trị mặc định sẽ được sử dụng:
- Volume: 80
- Mic Gain: 0
- Auto Wake: enabled

### 2. Điều chỉnh âm lượng

#### Bằng nút phần cứng:
- **Vol+**: Tăng âm lượng 10 đơn vị (tối đa 100)
- **Vol-**: Giảm âm lượng 10 đơn vị (tối thiểu 0)

Mỗi khi nhấn nút, âm lượng mới sẽ được:
1. Áp dụng ngay lập tức cho codec
2. **Lưu tự động vào NVS**
3. Log ra console: `Volume set to XX (saved)`

### 3. Khôi phục cài đặt

Khi ESP32 khởi động lại:
```
I (XXX) SETTINGS: Initializing settings module
I (XXX) SETTINGS: Loaded volume: 70
I (XXX) SETTINGS: Settings loaded from NVS
I (XXX) LYRAT_MINI_WS: Volume restored from settings: 70
```

Âm lượng cuối cùng bạn đã đặt sẽ được tự động khôi phục!

## API cho Developer

### Khởi tạo
```c
#include "settings.h"

// Trong app_main()
esp_err_t ret = settings_init();
```

### Đọc cài đặt
```c
// Lấy volume hiện tại
int volume = settings_get_volume();

// Lấy tất cả cài đặt
app_settings_t settings;
settings_get(&settings);
```

### Ghi cài đặt
```c
// Lưu volume (tự động commit vào NVS)
settings_set_volume(80);

// Lưu toàn bộ cài đặt
app_settings_t settings = {
    .volume = 75,
    .mic_gain = 0,
    .auto_wake = true
};
settings_save(&settings);
```

### Reset về mặc định
```c
settings_reset();
```

## Cấu trúc dữ liệu

### NVS Namespace: `app_settings`

| Key | Type | Range | Default |
|-----|------|-------|---------|
| volume | i32 | 0-100 | 80 |
| mic_gain | i32 | -10 to 10 | 0 |
| auto_wake | u8 | 0 or 1 | 1 |

## Thread Safety

Module này sử dụng **FreeRTOS Mutex** để đảm bảo thread-safe khi đọc/ghi cài đặt từ nhiều task.

## Kiểm tra

### 1. Flash firmware mới
```bash
# Dừng monitor nếu đang chạy (Ctrl+])
cd /Users/promex04/Desktop/guild/speech_recognition/wwe/LyratMini_RecordPlay
idf.py flash monitor
```

### 2. Kiểm tra log khởi động
Bạn sẽ thấy:
```
I (xxx) SETTINGS: Initializing settings module
I (xxx) SETTINGS: Volume not found in NVS, using default: 80  // Lần đầu
I (xxx) SETTINGS: Settings loaded from NVS
I (xxx) LYRAT_MINI_WS: Volume restored from settings: 80
```

### 3. Test thay đổi volume
- Nhấn **Vol+** nhiều lần
- Nhấn **Vol-** nhiều lần
- Quan sát log: `[ * ] Volume set to XX (saved)`

### 4. Test persistence
- **Khởi động lại**: Nhấn nút Reset hoặc rút nguồn
- Kiểm tra log: Volume được khôi phục với giá trị cuối cùng bạn đã đặt

### 5. Test xóa NVS (reset to defaults)
```bash
idf.py erase-flash
idf.py flash monitor
```
Volume sẽ trở về giá trị mặc định: 80

## Troubleshooting

### Volume không được lưu
- Kiểm tra log: Có thể có lỗi `Failed to save volume: ...`
- Nguyên nhân: NVS có thể bị đầy hoặc lỗi phần cứng
- Giải pháp: `idf.py erase-flash` và flash lại

### Volume không được khôi phục
- Kiểm tra log khởi động: có thể thấy `Volume not found in NVS`
- Nguyên nhân: Lần đầu chạy hoặc NVS đã bị xóa
- Giải pháp: Điều chỉnh volume một lần, nó sẽ được lưu

## Mở rộng

### Thêm cài đặt mới

1. **Cập nhật `settings.h`**:
```c
typedef struct {
    int volume;
    int mic_gain;
    bool auto_wake;
    int new_setting;  // Thêm cài đặt mới
} app_settings_t;

int settings_get_new_setting(void);
esp_err_t settings_set_new_setting(int value);
```

2. **Cập nhật `settings.c`**:
- Thêm vào `settings_init()` để load
- Thêm vào `settings_save()` để lưu
- Implement các getter/setter

3. **Sử dụng trong `main_ws.c`** hoặc code khác

## Tài liệu tham khảo

- [ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
- [FreeRTOS Semaphore](https://www.freertos.org/Documentation/02-Kernel/04-API-references/10-Semaphore-and-mutexes/00-Semaphore)
