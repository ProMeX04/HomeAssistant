#ifndef _CONFIG_H_
#define _CONFIG_H_

// ============================================================================
// JARVIS v4 - Optimized for ESP32-LyraT-Mini (4MB PSRAM, ESP32 not S3)
// Based on ESP-ADF AI Agent examples but tuned for weaker hardware
// ============================================================================

// ============================================================================
// WiFi Configuration
// ============================================================================
#define WIFI_SSID               "Nguyen Van Hai"
#define WIFI_PASS               "0964822864"
#define WIFI_RETRY_COUNT        5

// ============================================================================
// WebSocket Configuration - Optimized for stability
// ============================================================================
#define WS_URI                  "ws://laihieu2714.ddns.net:6666"
#define WS_BUFFER_SIZE          (64 * 1024)    // Reduced from 128KB - ESP32 has limited internal RAM
#define WS_PING_INTERVAL_SEC    15             // Increased to reduce overhead
#define WS_RETRY_MAX            10             // Reduced retries
#define WS_RETRY_DELAY_MS       1000           // Increased delay between retries
#define WS_MAX_RETRY_DELAY_MS   10000
#define WS_SEND_TIMEOUT_MS      5000           // Reduced - fail faster
#define WS_CONNECT_TIMEOUT_MS   8000

// ============================================================================
// Audio I2S Configuration
// ============================================================================
#define I2S_NUM_PLAY            0
#define I2S_NUM_REC             1
#define REC_SAMPLE_RATE         16000
#define REC_BITS                16
#define REC_CHANNELS            1
#define REC_TIME_SEC            10
#define PLAY_SAMPLE_RATE        48000   

// ============================================================================
// Audio Buffer Configuration - CONSERVATIVE for ESP32 (4MB PSRAM)
// Total PSRAM usage target: ~1.5MB (leaving room for WakeNet model ~1.5MB)
// ============================================================================
#define RAW_WRITE_BUFFER_SIZE   (384 * 1024)   // Playback buffer - ~6s MP3 @ 128kbps
#define I2S_WRITE_BUFFER_SIZE   (128 * 1024)   // I2S output buffer
#define RAW_READ_BUFFER_SIZE    (64 * 1024)    // Recording buffer
#define AUDIO_CHUNK_SIZE        2048           // Smaller chunks = lower latency, more CPU
#define AUDIO_WRITE_TIMEOUT_MS  1500

// ============================================================================
// AFE (Audio Front-End) Configuration - LITE for ESP32
// ESP32 is slower than S3, so we disable heavy features
// ============================================================================

// 3A Processing Flags (1 = enable, 0 = disable)
#define AFE_ENABLE_AEC          0              // AEC OFF - ESP32 struggles with this + WakeNet
#define AFE_ENABLE_AGC          0              // AGC OFF - let server handle gain
#define AFE_ENABLE_VAD          1              // VAD ON - critical for latency reduction
#define AFE_ENABLE_SE           0              // SE OFF - needs 2+ mics
#define AFE_ENABLE_NS           0              // NS OFF - too CPU heavy

// AFE performance settings - Conservative for ESP32
#define AFE_LINEAR_GAIN         1.2f           // Slight boost to compensate for no AGC
#define AFE_RINGBUF_SIZE        50             // Reduced from 100 - less memory
#define AFE_VAD_MODE            2              // Mode 2 = balanced (not too aggressive)

// VAD timing (on-device) - Tuned for natural speech
#define AFE_VAD_MIN_SPEECH_MS   150            // Ignore very short sounds
#define AFE_VAD_MIN_NOISE_MS    600            // Faster silence detection
#define AFE_VAD_DELAY_MS        100            // Debounce
#define AFE_WAKEUP_TIMEOUT_MS   45000          // Reduced from 60s

// ============================================================================
// Task Configuration - Optimized priorities for ESP32 dual-core
// Core 0: WiFi, WebSocket, Playback
// Core 1: WakeNet, Recording, AFE
// ============================================================================
#define STREAM_TASK_STACK_SIZE  8192           // Reduced - optimized code
#define STREAM_TASK_PRIORITY    6              // Lower than WakeNet
#define STREAM_TASK_CORE        0              // Core 0 with networking

#define RECORDER_TASK_PRIORITY  12             // Higher - WakeNet is critical
#define RECORDER_TASK_CORE      1              // Core 1 for audio processing

#define PLAYBACK_TASK_PRIORITY  8
#define PLAYBACK_TASK_CORE      0

// ============================================================================
// Server-side VAD Configuration (used as backup)
// On-device VAD is primary, server VAD is secondary verification
// ============================================================================
#define SERVER_VAD_ENABLED      1              // Enable server VAD as backup
#define VAD_RMS_THRESHOLD       800            // Lower threshold - device VAD already filtered
#define VAD_WAIT_TIMEOUT_MS     8000           // Reduced timeout
#define VAD_PREROLL_CHUNKS      3              // Less preroll needed with on-device VAD
#define VAD_MIN_SPEECH_CHUNKS   2              // Already filtered by device
#define VAD_SILENCE_CHUNKS      3              // Faster cutoff

// ============================================================================
// Streaming Optimization - NEW
// ============================================================================
#define STREAM_BATCH_SIZE       3              // Send 3 chunks at once to reduce WS overhead
#define STREAM_YIELD_MS         5              // Yield time between batches
#define STREAM_MAX_DURATION_MS  15000          // Max recording duration

// ============================================================================
// Misc Configuration
// ============================================================================
#define CODEC_VOLUME_PERCENT    75             // Reduced - less echo issues
#define PROGRESS_LOG_INTERVAL   100000         // Less frequent logging
#define TONE_PLAYBACK_POLL_MS   50             // Faster poll
#define AI_RESPONSE_TIMEOUT_MS  45000          // Reduced timeout

// ============================================================================
// Feature Flags
// ============================================================================
#define FEATURE_BARGE_IN        1              // Allow interrupting playback
#define FEATURE_CONTINUOUS_LISTEN 0            // 0 = wake word required
#define FEATURE_AUDIO_ENCODING  0              // 0 = PCM (OPUS needs ESP32-S3)
#define FEATURE_ON_DEVICE_VAD   1              // NEW: Use on-device VAD
#define FEATURE_SMART_SILENCE   1              // NEW: Stop early on silence

// ============================================================================
// Debug Flags
// ============================================================================
#define DEBUG_AUDIO_TIMING      0              // Log audio processing times
#define DEBUG_VAD_STATE         0              // Log VAD state changes
#define DEBUG_MEMORY            0              // Log memory usage

#endif // _CONFIG_H_
