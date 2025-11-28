# Feature Configuration Guide

## Overview
Bạn có thể enable/disable từng feature để tiết kiệm tài nguyên và chỉ chạy những gì cần thiết.

## Configuration File
Mở file: `main/config.h`

## Available Options

### 1. ENABLE_WAKE_WORD_MODE
```c
#define ENABLE_WAKE_WORD_MODE   1   // 1=Enable, 0=Disable
```

**Khi ENABLE (1):**
- ✅ Wake word detection ("Jarvis")
- ✅ WebSocket connection
- ✅ Audio streaming to server
- ✅ AFE (Audio Front End) + WakeNet
- ❌ Tốn ~30% CPU + ~150KB RAM

**Khi DISABLE (0):**
- ✅ Tiết kiệm CPU và RAM
- ✅ Không có AFE warnings
- ❌ Không có wake word detection
- ❌ Không có WebSocket

### 2. ENABLE_MP3_PLAYER_MODE
```c
#define ENABLE_MP3_PLAYER_MODE  1   // 1=Enable, 0=Disable
```

**Khi ENABLE (1):**
- ✅ SD card mount
- ✅ MP3 playback
- ✅ Playlist management
- ✅ PLAY/PAUSE/NEXT/PREV controls
- ❌ Tốn ~20% CPU + ~100KB RAM

**Khi DISABLE (0):**
- ✅ Tiết kiệm CPU và RAM
- ✅ Không cần SD card
- ❌ Không có MP3 player
- ❌ Buttons PLAY/REC/SET không hoạt động

### 3. DEFAULT_STARTUP_MODE
```c
#define DEFAULT_STARTUP_MODE    0   // 0=Wake Word, 1=MP3 Player
```

**Chỉ áp dụng khi CẢ 2 MODE đều ENABLED**

- `0`: Khởi động vào **Wake Word Mode**
- `1`: Khởi động vào **MP3 Player Mode**

## Use Cases

### Use Case 1: Chỉ MP3 Player (tiết kiệm tối đa)
```c
#define ENABLE_WAKE_WORD_MODE   0  
#define ENABLE_MP3_PLAYER_MODE  1  
#define DEFAULT_STARTUP_MODE    1  // Không quan trọng vì chỉ có 1 mode
```
**Kết quả:**
- Không có WebSocket
- Không có AFE/WakeNet
- Không có wake word
- Chỉ có MP3 player
- **Tiết kiệm ~150KB RAM, 30% CPU**

### Use Case 2: Chỉ Wake Word (không cần MP3)
```c
#define ENABLE_WAKE_WORD_MODE   1  
#define ENABLE_MP3_PLAYER_MODE  0  
#define DEFAULT_STARTUP_MODE    0  // Không quan trọng
```
**Kết quả:**
- Không mount SD card
- Không có MP3 player
- Chỉ có wake word detection
- **Tiết kiệm ~100KB RAM, 20% CPU**

### Use Case 3: Cả 2 features (full functionality)
```c
#define ENABLE_WAKE_WORD_MODE   1  
#define ENABLE_MP3_PLAYER_MODE  1  
#define DEFAULT_STARTUP_MODE    0  // Bắt đầu ở Wake Word mode
```
**Kết quả:**
- Full features
- Có thể toggle giữa 2 modes bằng nút MODE
- Tốn nhiều tài nguyên nhất

### Use Case 4: MP3 first, có thể chuyển sang Wake Word
```c
#define ENABLE_WAKE_WORD_MODE   1  
#define ENABLE_MP3_PLAYER_MODE  1  
#define DEFAULT_STARTUP_MODE    1  // Bắt đầu ở MP3 Player mode
```
**Kết quả:**
- Khởi động thẳng vào MP3 mode
- AFE/WakeNet chỉ khởi tạo khi cần
- Tiết kiệm resources khi không dùng wake word

## Button Behavior

### Khi CẢ 2 MODE enabled:
- **MODE**: Toggle giữa Wake Word ↔ MP3 Player
- **PLAY**: Play/Pause (chỉ MP3 mode)
- **REC**: Next track (chỉ MP3 mode)
- **SET**: Previous track (chỉ MP3 mode)
- **VOL+/VOL-**: Volume control (always)

### Khi CHỈ MP3 enabled:
- **MODE**: Không hoạt động
- **PLAY**: Play/Pause
- **REC**: Next track
- **SET**: Previous track
- **VOL+/VOL-**: Volume control

### Khi CHỈ Wake Word enabled:
- **MODE**: Không hoạt động
- **PLAY/REC/SET**: Không hoạt động
- **VOL+/VOL-**: Volume control

## Resource Comparison

| Configuration | RAM Usage | CPU Usage | Features |
|--------------|-----------|-----------|----------|
| Both Disabled | - | - | ❌ Nothing works |
| Wake Word Only | ~150KB | ~30% | ✅ Voice assistant |
| MP3 Only | ~100KB | ~20% | ✅ Music player |
| Both Enabled (WW active) | ~250KB | ~50% | ✅ Full features |
| Both Enabled (MP3 active) | ~250KB | ~35% | ✅ Full features |

## Build & Flash

Sau khi chỉnh sửa `config.h`:

```bash
idf.py build flash monitor
```

## Notes

- ⚠️ Nếu disable cả 2 modes → device sẽ chỉ có volume control
- ✅ Thay đổi config phải rebuild project
- ✅ Compile-time configuration → không thể thay đổi runtime
- ✅ Mỗi mode chỉ init những gì cần → clean resources
