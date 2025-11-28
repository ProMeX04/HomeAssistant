#ifndef _CONFIG_H_
#define _CONFIG_H_

// ========================================
// FEATURE TOGGLES
// ========================================
// Enable/Disable features to save resources
#define ENABLE_WAKE_WORD_MODE   1   // 1=Enable wake word detection, 0=Disable
#define ENABLE_MP3_PLAYER_MODE  1   // 1=Enable MP3 player from SD, 0=Disable
#define ENABLE_BLUETOOTH_MODE   1   // 1=Enable Bluetooth speaker, 0=Disable

// Default mode on startup (0=Wake Word, 1=MP3, 2=Bluetooth)
#define DEFAULT_STARTUP_MODE    1   // Start in MP3 mode

// Bluetooth Configuration
#define BT_DEVICE_NAME          "ESP32-LyraT-Speaker"
#define BT_DISCOVERABLE         1   // Always discoverable

// WiFi Configuration
#define WIFI_SSID               "Nguyen Van Hai"
#define WIFI_PASS               "0964822864"
#define WIFI_RETRY_COUNT        5

// WebSocket Configuration
#define WS_URI                  "ws://laihieu2714.ddns.net:6666/audio"
#define WS_BUFFER_SIZE          4096
#define WS_PING_INTERVAL_SEC    10
#define WS_RETRY_MAX            20
#define WS_RETRY_DELAY_MS       500
#define WS_MAX_RETRY_DELAY_MS   8000
#define WS_SEND_TIMEOUT_MS      5000
#define WS_CONNECT_TIMEOUT_MS   10000

// Audio Configuration
#define I2S_NUM_PLAY            0
#define I2S_NUM_REC             1
#define REC_SAMPLE_RATE         16000
#define REC_BITS                16
#define REC_CHANNELS            1
#define REC_TIME_SEC            10

// Buffer Sizes
#define RAW_WRITE_BUFFER_SIZE   (32 * 1024) // Increased to 32KB for burst handling
#define I2S_WRITE_BUFFER_SIZE   (16 * 1024) // Increased to 16KB for smoother playback
#define RAW_READ_BUFFER_SIZE    (64 * 1024)
#define AUDIO_CHUNK_SIZE        4096
#define AUDIO_WRITE_TIMEOUT_MS  2000 // Timeout for writing audio data

// Task Configuration
#define STREAM_TASK_STACK_SIZE  4096
#define STREAM_TASK_PRIORITY    5
#define RECORDER_TASK_PRIORITY  10

// VAD & Silence Skipping
#define VAD_RMS_THRESHOLD       1000  // Threshold for silence detection (adjust based on mic)
#define VAD_WAIT_TIMEOUT_MS     5000  // Max time to wait for speech before aborting
#define VAD_BUFFER_PRE_ROLL     2     // Number of chunks to keep before speech starts (optional)
#define VAD_IGNORE_CHUNKS       3     // Number of chunks to ignore after wake word (residue)

// Misc Configuration
#define CODEC_VOLUME_PERCENT    80
#define PROGRESS_LOG_INTERVAL   50000
#define TONE_PLAYBACK_POLL_MS   100
#define AI_RESPONSE_TIMEOUT_MS  60000  // Max time to wait for AI response (60s)

#endif // _CONFIG_H_
