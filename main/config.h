#ifndef _CONFIG_H_
#define _CONFIG_H_

// WiFi Configuration
#define WIFI_SSID               "Nguyen Van Hai"
#define WIFI_PASS               "0964822864"
#define WIFI_RETRY_COUNT        5

// WebSocket Configuration
#define WS_URI                  "ws://laihieu2714.ddns.net:6666"
#define WS_BUFFER_SIZE          (128 * 1024)  // 128KB Network Buffer (Enhanced for burst traffic)
#define WS_PING_INTERVAL_SEC    10
#define WS_RETRY_MAX            20
#define WS_RETRY_DELAY_MS       500
#define WS_MAX_RETRY_DELAY_MS   8000
#define WS_SEND_TIMEOUT_MS      10000
#define WS_CONNECT_TIMEOUT_MS   10000

// Audio Configuration
#define I2S_NUM_PLAY            0
#define I2S_NUM_REC             1
#define REC_SAMPLE_RATE         16000
#define REC_BITS                16
#define REC_CHANNELS            1
#define REC_TIME_SEC            10
#define PLAY_SAMPLE_RATE        48000 // High Quality Audio

// Buffer Sizes (Optimized PSRAM - Total ~2.75MB)
#define RAW_WRITE_BUFFER_SIZE   (2 * 1024 * 1024) // 2MB Jitter Buffer (~21s audio at 48kHz)
#define I2S_WRITE_BUFFER_SIZE   (512 * 1024)      // 512KB Playback Buffer
#define RAW_READ_BUFFER_SIZE    (256 * 1024)      // 256KB Recording Buffer
#define AUDIO_CHUNK_SIZE        8192              // 8KB chunks
#define AUDIO_WRITE_TIMEOUT_MS  2000

// Task Configuration
#define STREAM_TASK_STACK_SIZE  16384  // 16KB - Increased for WebSocket + VAD processing
#define STREAM_TASK_PRIORITY    8     // Increased priority for network stability
#define RECORDER_TASK_PRIORITY  10

// VAD & Silence Skipping - UPDATED CONFIGURATION
#define VAD_RMS_THRESHOLD       1000   // Based on noise floor: ~900 RMS when silent
#define VAD_WAIT_TIMEOUT_MS     10000  // Max time to wait for speech before aborting (10s)
#define VAD_PREROLL_CHUNKS      5      // INCREASED: Buffer 5 chunks (~640ms) before speech for context
#define VAD_MIN_SPEECH_CHUNKS   3      // Minimum chunks of speech before confirming (anti-noise)
#define VAD_SILENCE_CHUNKS      4      // Consecutive silent chunks to end speech (was MAX_SILENT_READS)

// Misc Configuration
#define CODEC_VOLUME_PERCENT    80
#define PROGRESS_LOG_INTERVAL   50000
#define TONE_PLAYBACK_POLL_MS   100
#define AI_RESPONSE_TIMEOUT_MS  60000  // Max time to wait for AI response (60s)

#endif // _CONFIG_H_
