/*
 * JARVIS v4 - Optimized for ESP32-LyraT-Mini
 * Based on ESP-ADF AI Agent examples, tuned for weaker hardware
 * 
 * Key optimizations:
 * - On-device VAD for faster silence detection
 * - Batched audio streaming to reduce WS overhead
 * - Conservative memory usage for 4MB PSRAM
 * - Optimized task priorities and core affinity
 * - Smart silence detection with timeout
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_button.h"
#include "input_key_service.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "esp_websocket_client.h"
#include "wifi_helper.h"
#include "tone_stream.h"
#include "mp3_decoder.h"
#include "filter_resample.h"
#include "esp_timer.h"
#include "config.h"
#include "settings.h"
#include "audio_recorder.h"
#include "recorder_sr.h"

static const char *TAG = "JARVIS";

// ============================================================================
// State Machine - Extended for on-device VAD
// ============================================================================
typedef enum {
    STATE_IDLE,           // Waiting for wake word
    STATE_LISTENING,      // Wake word detected, collecting speech
    STATE_STREAMING,      // Sending audio to server
    STATE_WAITING,        // Waiting for AI response
    STATE_PLAYING         // Playing AI response
} state_t;

static volatile state_t g_state = STATE_IDLE;
static SemaphoreHandle_t g_mutex = NULL;

// VAD state tracking (on-device)
static volatile int g_silence_chunks = 0;
static volatile int g_speech_chunks = 0;
static volatile bool g_speech_started = false;

static inline void set_state(state_t s) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    state_t old = g_state;
    g_state = s;
    xSemaphoreGive(g_mutex);
    
#if DEBUG_VAD_STATE
    ESP_LOGI(TAG, "State: %d → %d", old, s);
#endif
}

static inline state_t get_state(void) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    state_t s = g_state;
    xSemaphoreGive(g_mutex);
    return s;
}

// ============================================================================
// Global Variables - Minimal
// ============================================================================
static audio_pipeline_handle_t g_play_pipe = NULL;
static audio_element_handle_t g_raw_writer = NULL;
static audio_element_handle_t g_i2s_writer = NULL;

static audio_pipeline_handle_t g_rec_pipe = NULL;
static audio_element_handle_t g_raw_reader = NULL;

static esp_websocket_client_handle_t g_ws = NULL;
static audio_board_handle_t g_board = NULL;
static audio_rec_handle_t g_recorder = NULL;

static volatile bool g_flush = false;
static volatile bool g_playback_started = false;

// Streaming stats
static volatile int64_t g_stream_start_time = 0;
static volatile int g_total_bytes_sent = 0;

// ============================================================================
// Memory Debug Helper
// ============================================================================
#if DEBUG_MEMORY
static void log_memory(const char *label) {
    ESP_LOGI(TAG, "[%s] Heap: %lu | PSRAM: %lu", 
             label,
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
#else
#define log_memory(x)
#endif

// ============================================================================
// On-Device VAD Helper - Lightweight RMS-based
// ============================================================================
static inline int16_t calculate_rms(const int16_t *samples, int count) {
    if (count == 0) return 0;
    
    int64_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += (int32_t)samples[i] * samples[i];
    }
    
    // Fast integer sqrt approximation
    uint32_t mean = sum / count;
    uint32_t rms = 0;
    uint32_t bit = 1UL << 30;
    
    while (bit > mean) bit >>= 2;
    while (bit != 0) {
        if (mean >= rms + bit) {
            mean -= rms + bit;
            rms = (rms >> 1) + bit;
        } else {
            rms >>= 1;
        }
        bit >>= 2;
    }
    
    return (int16_t)rms;
}

// ============================================================================
// Playback Pipeline (MP3 from server) - Optimized buffer sizes
// ============================================================================
static void init_playback(void) {
    ESP_LOGI(TAG, "Init playback (buf: %dKB)", RAW_WRITE_BUFFER_SIZE / 1024);
    
    audio_pipeline_cfg_t cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    g_play_pipe = audio_pipeline_init(&cfg);
    
    // Raw input - reduced buffer
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_cfg.out_rb_size = RAW_WRITE_BUFFER_SIZE;
    g_raw_writer = raw_stream_init(&raw_cfg);
    
    // MP3 decoder - pin to core 0 with playback
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_core = PLAYBACK_TASK_CORE;
    mp3_cfg.task_prio = PLAYBACK_TASK_PRIORITY;
    audio_element_handle_t mp3 = mp3_decoder_init(&mp3_cfg);
    
    // Resample 44.1k -> 48k
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 44100;
    rsp_cfg.src_ch = 1;
    rsp_cfg.dest_rate = PLAY_SAMPLE_RATE;
    rsp_cfg.dest_ch = 1;
    audio_element_handle_t rsp = rsp_filter_init(&rsp_cfg);
    
    // I2S output - optimized settings
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = I2S_WRITE_BUFFER_SIZE;
    i2s_cfg.task_prio = 23;  // Highest priority for smooth playback
    i2s_cfg.task_core = PLAYBACK_TASK_CORE;
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = PLAY_SAMPLE_RATE;
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    g_i2s_writer = i2s_stream_init(&i2s_cfg);
    
    audio_pipeline_register(g_play_pipe, g_raw_writer, "raw");
    audio_pipeline_register(g_play_pipe, mp3, "mp3");
    audio_pipeline_register(g_play_pipe, rsp, "rsp");
    audio_pipeline_register(g_play_pipe, g_i2s_writer, "i2s");
    
    const char *link[] = {"raw", "mp3", "rsp", "i2s"};
    audio_pipeline_link(g_play_pipe, link, 4);
    
    log_memory("After playback init");
}

// ============================================================================
// Recording Pipeline - Optimized for on-device VAD
// ============================================================================
static void init_recording(void) {
    ESP_LOGI(TAG, "Init recording (buf: %dKB)", RAW_READ_BUFFER_SIZE / 1024);
    
    audio_pipeline_cfg_t cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    g_rec_pipe = audio_pipeline_init(&cfg);
    
    // I2S input - pin to core 1
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_cfg.chan_cfg.id = I2S_NUM_REC;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = REC_SAMPLE_RATE;
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    i2s_cfg.task_core = RECORDER_TASK_CORE;
    audio_element_handle_t i2s = i2s_stream_init(&i2s_cfg);
    
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_cfg.out_rb_size = RAW_READ_BUFFER_SIZE;
    g_raw_reader = raw_stream_init(&raw_cfg);
    
    audio_pipeline_register(g_rec_pipe, i2s, "i2s");
    audio_pipeline_register(g_rec_pipe, g_raw_reader, "raw");
    
    const char *link[] = {"i2s", "raw"};
    audio_pipeline_link(g_rec_pipe, link, 2);
    audio_pipeline_run(g_rec_pipe);
    
    log_memory("After recording init");
}

// ============================================================================
// Play Tone - Simplified with shorter delay
// ============================================================================
static void play_ding(void) {
    ESP_LOGI(TAG, "🔔 Ding!");
    
    // Temporary tone pipeline
    audio_pipeline_handle_t tone_pipe = NULL;
    audio_element_handle_t tone_src = NULL;
    audio_element_handle_t mp3_dec = NULL;
    audio_element_handle_t rsp = NULL;
    audio_element_handle_t i2s_out = NULL;
    
    // Stop main playback
    audio_pipeline_stop(g_play_pipe);
    audio_pipeline_wait_for_stop(g_play_pipe);
    
    // Create tone pipeline
    audio_pipeline_cfg_t cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    tone_pipe = audio_pipeline_init(&cfg);
    
    tone_stream_cfg_t tone_cfg = TONE_STREAM_CFG_DEFAULT();
    tone_cfg.type = AUDIO_STREAM_READER;
    tone_src = tone_stream_init(&tone_cfg);
    
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_dec = mp3_decoder_init(&mp3_cfg);
    
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 16000;
    rsp_cfg.src_ch = 1;
    rsp_cfg.dest_rate = PLAY_SAMPLE_RATE;
    rsp_cfg.dest_ch = 1;
    rsp = rsp_filter_init(&rsp_cfg);
    
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = PLAY_SAMPLE_RATE;
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    i2s_out = i2s_stream_init(&i2s_cfg);
    
    audio_pipeline_register(tone_pipe, tone_src, "tone");
    audio_pipeline_register(tone_pipe, mp3_dec, "mp3");
    audio_pipeline_register(tone_pipe, rsp, "rsp");
    audio_pipeline_register(tone_pipe, i2s_out, "i2s");
    
    const char *link[] = {"tone", "mp3", "rsp", "i2s"};
    audio_pipeline_link(tone_pipe, link, 4);
    
    audio_element_set_uri(tone_src, "flash://tone/0_dingdong.mp3");
    audio_pipeline_run(tone_pipe);
    
    // Shorter wait - 500ms is enough for ding
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Cleanup
    audio_pipeline_stop(tone_pipe);
    audio_pipeline_wait_for_stop(tone_pipe);
    audio_pipeline_unlink(tone_pipe);
    audio_pipeline_unregister(tone_pipe, tone_src);
    audio_pipeline_unregister(tone_pipe, mp3_dec);
    audio_pipeline_unregister(tone_pipe, rsp);
    audio_pipeline_unregister(tone_pipe, i2s_out);
    audio_element_deinit(tone_src);
    audio_element_deinit(mp3_dec);
    audio_element_deinit(rsp);
    audio_element_deinit(i2s_out);
    audio_pipeline_deinit(tone_pipe);
    
    // Reset main playback
    audio_pipeline_reset_ringbuffer(g_play_pipe);
    audio_pipeline_reset_elements(g_play_pipe);
    g_playback_started = false;
    
    ESP_LOGI(TAG, "Ding complete");
}

// ============================================================================
// WebSocket Handler - Optimized
// ============================================================================
static void ws_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_websocket_event_data_t *ws = (esp_websocket_event_data_t *)data;
    
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "🌐 Connected");
        // DON'T run pipeline yet - wait for audio
        g_playback_started = false;
        break;
        
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "❌ Disconnected");
        set_state(STATE_IDLE);
        g_playback_started = false;
        
        // Auto reconnect after delay
        vTaskDelay(pdMS_TO_TICKS(WS_RETRY_DELAY_MS));
        if (!esp_websocket_client_is_connected(g_ws)) {
            ESP_LOGI(TAG, "🔄 Reconnecting...");
            esp_websocket_client_start(g_ws);
        }
        break;
        
    case WEBSOCKET_EVENT_DATA:
        if (!ws || ws->data_len == 0) break;
        
        // Text commands
        if (ws->op_code == 0x01) {
            if (ws->data_len == 9 && memcmp(ws->data_ptr, "AUDIO_END", 9) == 0) {
                ESP_LOGI(TAG, "✅ Audio complete");
                set_state(STATE_IDLE);
            }
            else if (ws->data_len == 11 && memcmp(ws->data_ptr, "AUDIO_START", 11) == 0) {
                ESP_LOGI(TAG, "🎵 Audio starting");
                g_flush = false;
                set_state(STATE_PLAYING);
                
                // Reset and start playback fresh
                if (g_playback_started) {
                    audio_pipeline_stop(g_play_pipe);
                    audio_pipeline_wait_for_stop(g_play_pipe);
                    audio_pipeline_reset_ringbuffer(g_play_pipe);
                    audio_pipeline_reset_elements(g_play_pipe);
                }
                audio_pipeline_run(g_play_pipe);
                g_playback_started = true;
            }
            else if (ws->data_len == 14 && memcmp(ws->data_ptr, "STOP_RECORDING", 14) == 0) {
                ESP_LOGI(TAG, "🛑 Server: stop");
                if (get_state() == STATE_STREAMING || get_state() == STATE_LISTENING) {
                    set_state(STATE_WAITING);
                }
            }
        }
        // Binary audio data
        else if (ws->op_code == 0x02 && !g_flush && g_raw_writer) {
            if (!g_playback_started) {
                audio_pipeline_run(g_play_pipe);
                g_playback_started = true;
            }
            raw_stream_write(g_raw_writer, (char *)ws->data_ptr, ws->data_len);
        }
        break;
        
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "⚠️ WS Error");
        break;
    }
}

// ============================================================================
// Streaming Task - With on-device VAD and batching
// ============================================================================
static void stream_task(void *arg) {
    ESP_LOGI(TAG, "📤 Streaming started");
    
    g_stream_start_time = esp_timer_get_time();
    g_total_bytes_sent = 0;
    
    // Allocate buffer in PSRAM
    const int buf_size = AUDIO_CHUNK_SIZE * STREAM_BATCH_SIZE;
    uint8_t *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Buffer alloc failed");
        set_state(STATE_IDLE);
        vTaskDelete(NULL);
        return;
    }
    
    int batch_offset = 0;
    bool first_chunk = true;
    int silence_count = 0;
    int speech_count = 0;
    int total_chunks = 0;
    
    const int max_chunks = STREAM_MAX_DURATION_MS / (AUDIO_CHUNK_SIZE * 1000 / (REC_SAMPLE_RATE * 2));
    
    while (get_state() == STATE_STREAMING && esp_websocket_client_is_connected(g_ws)) {
        // Read audio chunk
        int len = audio_recorder_data_read(g_recorder, buf + batch_offset, 
                                           AUDIO_CHUNK_SIZE, pdMS_TO_TICKS(30));
        
        if (len > 0) {
            total_chunks++;
            
#if FEATURE_ON_DEVICE_VAD
            // On-device VAD check
            int16_t *samples = (int16_t *)(buf + batch_offset);
            int sample_count = len / 2;
            int16_t rms = calculate_rms(samples, sample_count);
            
            if (rms > VAD_RMS_THRESHOLD) {
                speech_count++;
                silence_count = 0;
                
#if DEBUG_VAD_STATE
                if (speech_count == 1) {
                    ESP_LOGI(TAG, "🎙️ Speech detected (RMS: %d)", rms);
                }
#endif
            } else {
                // Only count silence after speech started
                if (speech_count >= VAD_MIN_SPEECH_CHUNKS) {
                    silence_count++;
                    
                    // Smart silence detection
                    if (silence_count >= VAD_SILENCE_CHUNKS) {
                        ESP_LOGI(TAG, "🔇 Silence detected → sending");
                        
                        // Send remaining buffer
                        if (batch_offset > 0) {
                            esp_websocket_client_send_bin(g_ws, (char *)buf, batch_offset, 
                                                          pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
                            g_total_bytes_sent += batch_offset;
                        }
                        break;
                    }
                }
            }
#endif
            
            batch_offset += len;
            
            // Send batch when full
            if (batch_offset >= buf_size) {
                esp_websocket_client_send_bin(g_ws, (char *)buf, batch_offset, 
                                              pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
                g_total_bytes_sent += batch_offset;
                
                if (first_chunk) {
                    int64_t latency = (esp_timer_get_time() - g_stream_start_time) / 1000;
                    ESP_LOGI(TAG, "First batch: %lld ms", latency);
                    first_chunk = false;
                }
                
                batch_offset = 0;
            }
            
            // Timeout protection
            if (total_chunks >= max_chunks) {
                ESP_LOGW(TAG, "⏱️ Max duration reached");
                break;
            }
        }
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(STREAM_YIELD_MS));
    }
    
    // Send any remaining data
    if (batch_offset > 0 && esp_websocket_client_is_connected(g_ws)) {
        esp_websocket_client_send_bin(g_ws, (char *)buf, batch_offset, 
                                      pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
        g_total_bytes_sent += batch_offset;
    }
    
    // Send END signal
    if (esp_websocket_client_is_connected(g_ws)) {
        esp_websocket_client_send_text(g_ws, "END", 3, pdMS_TO_TICKS(1000));
    }
    
    // Stats
    int64_t duration_ms = (esp_timer_get_time() - g_stream_start_time) / 1000;
    ESP_LOGI(TAG, "📤 Sent %d bytes in %lld ms (%d chunks)", 
             g_total_bytes_sent, duration_ms, total_chunks);
    
    free(buf);
    
    if (!esp_websocket_client_is_connected(g_ws)) {
        ESP_LOGW(TAG, "Connection lost during stream");
        set_state(STATE_IDLE);
    } else {
        set_state(STATE_WAITING);
    }
    
    vTaskDelete(NULL);
}

// ============================================================================
// Wake Word Callback - Optimized
// ============================================================================
static int input_cb(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks) {
    return raw_stream_read(g_raw_reader, (char *)buffer, buf_sz);
}

static esp_err_t recorder_cb(audio_rec_evt_t *event, void *user_data) {
    state_t current = get_state();
    
    // Only respond to wake word in IDLE state
    if (event->type == AUDIO_REC_WAKEUP_START && current == STATE_IDLE) {
        ESP_LOGI(TAG, "🎤 JARVIS!");
        
        // Check connection
        if (!esp_websocket_client_is_connected(g_ws)) {
            ESP_LOGW(TAG, "⚠️ Not connected, reconnecting...");
            esp_websocket_client_start(g_ws);
            
            // Quick wait with timeout
            for (int i = 0; i < 20; i++) {
                vTaskDelay(pdMS_TO_TICKS(100));
                if (esp_websocket_client_is_connected(g_ws)) {
                    ESP_LOGI(TAG, "✅ Connected");
                    break;
                }
            }
            
            if (!esp_websocket_client_is_connected(g_ws)) {
                ESP_LOGE(TAG, "❌ Connection failed");
                return ESP_OK;
            }
        }
        
        // Barge-in: stop any playing audio
        if (g_playback_started) {
            g_flush = true;
            audio_pipeline_stop(g_play_pipe);
            esp_websocket_client_send_text(g_ws, "BARGE_IN", 8, pdMS_TO_TICKS(100));
        }
        
        // Play confirmation sound
        play_ding();
        
        // Reset VAD state
        g_silence_chunks = 0;
        g_speech_chunks = 0;
        g_speech_started = false;
        
        // Start streaming
        set_state(STATE_STREAMING);
        xTaskCreatePinnedToCore(
            stream_task, 
            "stream", 
            STREAM_TASK_STACK_SIZE,
            NULL, 
            STREAM_TASK_PRIORITY, 
            NULL, 
            STREAM_TASK_CORE
        );
    }
    
    return ESP_OK;
}

// ============================================================================
// Button Handler - Volume control
// ============================================================================
static esp_err_t button_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx) {
    if (evt->type != INPUT_KEY_SERVICE_ACTION_CLICK) return ESP_OK;
    
    int vol = 0;
    audio_hal_get_volume(g_board->audio_hal, &vol);
    
    if ((int)evt->data == INPUT_KEY_USER_ID_VOLUP) {
        vol = (vol >= 95) ? 100 : vol + 5;
        audio_hal_set_volume(g_board->audio_hal, vol);
        settings_set_volume(vol);
        ESP_LOGI(TAG, "🔊 Vol: %d", vol);
    }
    else if ((int)evt->data == INPUT_KEY_USER_ID_VOLDOWN) {
        vol = (vol <= 5) ? 0 : vol - 5;
        audio_hal_set_volume(g_board->audio_hal, vol);
        settings_set_volume(vol);
        ESP_LOGI(TAG, "🔉 Vol: %d", vol);
    }
    
    return ESP_OK;
}

// ============================================================================
// Main
// ============================================================================
void app_main(void) {
    // Suppress noisy logs
    esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_WARN);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_WARN);
    esp_log_level_set("AUDIO_EVT", ESP_LOG_NONE);
    esp_log_level_set("MP3_DECODER", ESP_LOG_WARN);
    esp_log_level_set("TONE_STREAM", ESP_LOG_WARN);
    esp_log_level_set("AFE", ESP_LOG_WARN);
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    
    ESP_LOGI(TAG, "╔════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     JARVIS v4 - LyraT-Mini        ║");
    ESP_LOGI(TAG, "║   Optimized for ESP32 (4MB PSRAM) ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════╝");
    
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Mutex
    g_mutex = xSemaphoreCreateMutex();
    
    // Settings
    settings_init();
    
    log_memory("After init");
    
    // WiFi
    ESP_LOGI(TAG, "📶 Connecting to %s", WIFI_SSID);
    wifi_init_sta(WIFI_SSID, WIFI_PASS, WIFI_RETRY_COUNT);
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power save for lower latency
    
    // Audio board
    g_board = audio_board_init();
    audio_hal_ctrl_codec(g_board->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(g_board->audio_hal, settings_get_volume());
    
    log_memory("After audio board");
    
    // Pipelines
    init_playback();
    init_recording();
    
    log_memory("After pipelines");
    
    // WebSocket
    esp_websocket_client_config_t ws_cfg = {
        .uri = WS_URI,
        .buffer_size = WS_BUFFER_SIZE,
        .ping_interval_sec = WS_PING_INTERVAL_SEC,
    };
    g_ws = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(g_ws, WEBSOCKET_EVENT_ANY, ws_handler, NULL);
    esp_websocket_client_start(g_ws);
    
    // Wait for connection with timeout
    int wait_count = 0;
    while (!esp_websocket_client_is_connected(g_ws) && wait_count < 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    if (esp_websocket_client_is_connected(g_ws)) {
        ESP_LOGI(TAG, "🌐 WebSocket ready");
    } else {
        ESP_LOGW(TAG, "⚠️ WebSocket not connected yet");
    }
    
    log_memory("After WebSocket");
    
    // Wake word engine (audio_recorder with WakeNet)
    // Configure AFE based on config.h settings
    recorder_sr_cfg_t sr_cfg = DEFAULT_RECORDER_SR_CFG("LM", "model", AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    sr_cfg.afe_cfg->wakenet_init = true;
    sr_cfg.afe_cfg->vad_init = AFE_ENABLE_VAD;
    sr_cfg.afe_cfg->aec_init = AFE_ENABLE_AEC;
    sr_cfg.afe_cfg->se_init = AFE_ENABLE_SE;
    sr_cfg.afe_cfg->afe_linear_gain = AFE_LINEAR_GAIN;
    sr_cfg.afe_cfg->afe_ringbuf_size = AFE_RINGBUF_SIZE;
    
#if AFE_ENABLE_VAD
    sr_cfg.afe_cfg->vad_mode = AFE_VAD_MODE;
#endif
    
    sr_cfg.multinet_init = false;  // No command recognition needed
    
    audio_rec_cfg_t rec_cfg = AUDIO_RECORDER_DEFAULT_CFG();
    rec_cfg.task_prio = RECORDER_TASK_PRIORITY;
    rec_cfg.task_size = 6 * 1024;  // Reduced from 8KB
    rec_cfg.read = (recorder_data_read_t)input_cb;
    rec_cfg.sr_handle = recorder_sr_create(&sr_cfg, &rec_cfg.sr_iface);
    rec_cfg.event_cb = recorder_cb;
    rec_cfg.vad_off = 0;
    
    g_recorder = audio_recorder_create(&rec_cfg);
    
    log_memory("After WakeNet");
    
    // Buttons
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    audio_board_key_init(set);
    
    input_key_service_info_t key_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_cfg_t key_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    key_cfg.handle = set;
    
    periph_service_handle_t input = input_key_service_create(&key_cfg);
    input_key_service_add_key(input, key_info, INPUT_KEY_NUM);
    periph_service_set_callback(input, button_cb, NULL);
    
    // Ready!
    ESP_LOGI(TAG, "════════════════════════════════════");
    ESP_LOGI(TAG, "🎤 Ready! Say 'Jarvis'");
    ESP_LOGI(TAG, "   Features: On-device VAD, Batching");
    ESP_LOGI(TAG, "   Heap: %lu | PSRAM: %lu", 
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "════════════════════════════════════");
    
    // Main loop - periodic status
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));  // Every 30s
        
#if DEBUG_MEMORY
        log_memory("Periodic");
#endif
    }
}
