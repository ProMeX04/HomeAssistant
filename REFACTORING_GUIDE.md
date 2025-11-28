# Code Refactoring Guide - Module Architecture

## 📋 Overview
Tách `main_ws.c` (1200+ lines) thành các module riêng biệt để dễ maintain và extend.

## 🎯 New Architecture

```
main/
├── main_ws.c              # 200 lines - Main controller only
├── config.h               # Configuration
├── wake_word_mode.c/h     # 300 lines - Wake word module
├── mp3_player_mode.c/h    # 400 lines - MP3 player module
├── bluetooth_mode.c/h     # 300 lines - Bluetooth speaker module
└── CMakeLists.txt         # Updated build config
```

## 📝 Implementation Steps

### Step 1: Create MP3 Player Module

**File: `mp3_player_mode.h`** ✅ CREATED
- Public API declarations
- Function prototypes

**File: `mp3_player_mode.c`** 
Move from main_ws.c:
- `scan_mp3_files()`
- `init_mp3_pipeline()`
- `mp3_play_track()`
- `mp3_next_track()`
- `mp3_prev_track()`
- `mp3_pause()`  
- `mp3_resume()`
- `mp3_stop()`
- `mp3_monitor_task()`
- All MP3 state variables

### Step 2: Create Wake Word Module

**File: `wake_word_mode.h`**
```c
esp_err_t wake_word_mode_init(void);
void wake_word_mode_deinit(void);
void wake_word_mode_start(void);
void wake_word_mode_stop(void);
```

**File: `wake_word_mode.c`**
Move from main_ws.c:
- `recorder_event_cb()`
- `input_cb_for_afe()`
- `calculate_rms()`
- `stream_to_server_task()`
- `init_play_pipeline()`
- WebSocket handling
- All Wake Word state variables

### Step 3: Create Bluetooth Module

**File: `bluetooth_mode.h`**
```c
esp_err_t bluetooth_mode_init(void);
void bluetooth_mode_deinit(void);
void bluetooth_mode_start(void);
void bluetooth_mode_stop(void);
bool bluetooth_is_connected(void);
```

**File: `bluetooth_mode.c`**
New implementation:
- A2DP sink initialization
- Bluetooth callbacks
- Audio pipeline for BT stream
- Connection management

### Step 4: Simplify main_ws.c

**Keep only:**
```c
// Mode management
static system_mode_t current_mode = MODE_WAKE_WORD;

// Button handler
static esp_err_t input_key_service_cb(...) {
    switch (button_id) {
        case MODE:
            toggle_mode();
            break;
        case PLAY:
            if (current_mode == MP3) mp3_pause();
            break;
        // ...
    }
}

// Mode switching
void switch_to_mp3_mode() {
    wake_word_mode_stop();
    mp3_mode_start();
    current_mode = MODE_MP3_PLAYER;
}

void switch_to_wake_word_mode() {
    mp3_mode_stop();
    bluetooth_mode_stop();
    wake_word_mode_start();
    current_mode = MODE_WAKE_WORD;
}

void switch_to_bluetooth_mode() {
    wake_word_mode_stop();
    mp3_mode_stop();
    bluetooth_mode_start();
    current_mode = MODE_BLUETOOTH;
}

// Main
void app_main() {
    // Common init
    audio_board_init();
    
    #if ENABLE_WAKE_WORD_MODE
    wake_word_mode_init();
    #endif
    
    #if ENABLE_MP3_PLAYER_MODE
    mp3_mode_init();
    #endif
    
    #if ENABLE_BLUETOOTH_MODE
    bluetooth_mode_init();
    #endif
    
    // Start default mode
    switch (DEFAULT_STARTUP_MODE) {
        case 0: wake_word_mode_start(); break;
        case 1: mp3_mode_start(); break;
        case 2: bluetooth_mode_start(); break;
    }
    
    // Button init
    audio_board_key_init(set);
    input_key_service_create(...);
}
```

### Step 5: Update CMakeLists.txt

```cmake
idf_component_register(
    SRCS 
        "main_ws.c"
        "wake_word_mode.c"
        "mp3_player_mode.c"
        "bluetooth_mode.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        # ... existing components
)
```

## 🎯 Benefits

### Before (Current):
```
main_ws.c: 1200+ lines
├─ Wake Word code: ~400 lines
├─ MP3 Player code: ~400 lines  
├─ WebSocket code: ~200 lines
├─ Button handling: ~100 lines
└─ Misc: ~100 lines
```
**Problems:**
- 😵 Hard to navigate
- 😵 Merge conflicts
- 😵 Difficult to test individual features
- 😵 Global state everywhere

### After (Modular):
```
main_ws.c: ~200 lines (controller only)
wake_word_mode.c: ~300 lines
mp3_player_mode.c: ~400 lines
bluetooth_mode.c: ~300 lines
```
**Benefits:**
- ✅ Easy to find code
- ✅ Clean interfaces
- ✅ Easy to test modules separately
- ✅ Better encapsulation
- ✅ Parallel development possible

## 🚀 Migration Strategy

### Option 1: Gradual (Recommended)
1. ✅ Create headers first
2. Create .c files with skeleton
3. Copy-paste functions one by one
4. Test after each module
5. Remove from main_ws.c when working

### Option 2: Full Refactor
1. Create all files at once
2. Move all code
3. Fix compilation errors
4. Test everything

## 📌 Current Status

✅ `mp3_player_mode.h` created
⏳ Need to create:
- `mp3_player_mode.c`
- `wake_word_mode.h/c`
- `bluetooth_mode.h/c`
- Update `CMakeLists.txt`
- Refactor `main_ws.c`

## 🤔 Next Decision

**LỰA CHỌN 1: Tôi refactor toàn bộ ngay**
- Pro: Clean structure immediately
- Con: Mất 30-60 phút, nhiều code changes

**LỰA CHỌN 2: Guide cho bạn tự refactor từ từ**
- Pro: Bạn học được cách tổ chức code
- Con: Mất nhiều thời gian hơn

**LỰA CHỌN 3: Giữ nguyên hiện tại, chỉ add Bluetooth vào main_ws.c**
- Pro: Nhanh nhất, code vẫn chạy
- Con: File càng lúc càng lớn

Bạn muốn làm theo cách nào? 🤔
