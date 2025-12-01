/*
 * ESP32-LyraT-Mini V1.2 Wake Word & Audio Streaming với WebSocket
 * 
 * Features:
 * 1. Wake Word Detection ("Jarvis") using ESP-SR WakeNet
 * 2. Real-time Audio Streaming to server via WebSocket
 * 3. MP3 response playback từ server
 * 
 * Optimizations:
 * - Persistent tone pipeline to reduce memory allocation
 * - Improved error handling and retry logic
 * - Optimized buffer sizes for performance
 * - Better resource cleanup and leak prevention
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#include "audio_recorder.h"
#include "recorder_sr.h"
#include "esp_websocket_client.h"
#include "wifi_helper.h"
#include "tone_stream.h"
#include "fatfs_stream.h"
#include "mp3_decoder.h"
#include "filter_resample.h"
#include <math.h>
#include "esp_timer.h"
#include "ringbuf.h"
#include "config.h"
#include "settings.h"

static const char *TAG = "LYRAT_MINI_WS";

static bool is_recording = false;
static bool ai_response_complete = false;  // Track AUDIO_END from server
static audio_rec_handle_t recorder = NULL;
static audio_element_handle_t raw_read_el = NULL;
static esp_websocket_client_handle_t ws_client = NULL;
static TaskHandle_t stream_task_handle = NULL;
static bool flush_audio_flag = false; // Flag to drop stale audio packets


static audio_pipeline_handle_t pipeline_play = NULL;
static audio_element_handle_t raw_write_el = NULL;

/**
 * @brief Calculate Root Mean Square (RMS) of audio buffer
 * @param buffer Audio buffer (16-bit)
 * @param len Length in bytes
 * @return RMS value
 */
static float calculate_rms(int16_t *buffer, int len) {
    long long sum = 0;
    int samples = len / 2;
    if (samples == 0) return 0;

    for (int i = 0; i < samples; i++) {
        sum += buffer[i] * buffer[i];
    }
    
    return sqrt(sum / samples);
}



static audio_element_handle_t i2s_writer_el = NULL;

static void init_play_pipeline(void) {
    ESP_LOGI(TAG, "Initializing Playback Pipeline...");
    audio_pipeline_cfg_t pipeline_play_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_play = audio_pipeline_init(&pipeline_play_cfg);

    // Raw Stream Writer (receives PCM from WebSocket)
    raw_stream_cfg_t raw_write_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_write_cfg.type = AUDIO_STREAM_READER; // Source for pipeline
    raw_write_cfg.out_rb_size = RAW_WRITE_BUFFER_SIZE;
    // raw_write_cfg.write_time = pdMS_TO_TICKS(1000); // Removed: Not supported in this ADF version
    raw_write_el = raw_stream_init(&raw_write_cfg);

    // I2S Writer
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = I2S_WRITE_BUFFER_SIZE;
    i2s_cfg.task_prio = 12; // Higher priority for audio playback
    i2s_cfg.task_core = 0; // Pin to core 0
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = PLAY_SAMPLE_RATE; // Match server PCM output (48kHz)
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    i2s_writer_el = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline_play, raw_write_el, "raw_write");
    audio_pipeline_register(pipeline_play, i2s_writer_el, "i2s_writer");
    audio_pipeline_link(pipeline_play, (const char *[]) {"raw_write", "i2s_writer"}, 2);
    
    audio_pipeline_run(pipeline_play);
}



static SemaphoreHandle_t s_play_tone_sem = NULL;



static void play_tone_logic(void) {
    const char *uri = "flash://tone/0_dingdong.mp3";
    ESP_LOGI(TAG, "Playing tone: %s", uri);

    // Stop main playback pipeline temporarily
    if (pipeline_play) {
        audio_pipeline_pause(pipeline_play);
        // Clear ringbuffer to stop music immediately
        if (raw_write_el) {
            audio_element_reset_output_ringbuf(raw_write_el);
            audio_element_reset_state(raw_write_el);
        }
    }

    audio_pipeline_handle_t tone_pipeline;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    tone_pipeline = audio_pipeline_init(&pipeline_cfg);

    // Use tone_stream to read from Flash Partition
    tone_stream_cfg_t tone_cfg = TONE_STREAM_CFG_DEFAULT();
    tone_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t tone_source = tone_stream_init(&tone_cfg);
    audio_element_set_uri(tone_source, uri);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    audio_element_handle_t mp3_decoder = mp3_decoder_init(&mp3_cfg);

    // Resample Element to ensure 48kHz output
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 16000; // Initial guess, decoder will update
    rsp_cfg.src_ch = 1;
    rsp_cfg.dest_rate = 48000; // Target rate
    rsp_cfg.dest_ch = 1;
    audio_element_handle_t resample = rsp_filter_init(&rsp_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY; 
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 48000; 
    audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(tone_pipeline, tone_source, "tone_src");
    audio_pipeline_register(tone_pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(tone_pipeline, resample, "resample");
    audio_pipeline_register(tone_pipeline, i2s_writer, "i2s");

    const char *link_tag[4] = {"tone_src", "mp3", "resample", "i2s"};
    audio_pipeline_link(tone_pipeline, &link_tag[0], 4);

    audio_pipeline_run(tone_pipeline);

    // Wait for completion using event listener
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(tone_pipeline, evt);
    
    while (1) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, pdMS_TO_TICKS(2000)) != ESP_OK) {
            break; // Timeout
        }
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int) msg.data == AEL_STATUS_STATE_FINISHED) {
            break;
        }
    }

    audio_pipeline_stop(tone_pipeline);
    audio_pipeline_wait_for_stop(tone_pipeline);
    audio_pipeline_terminate(tone_pipeline);
    audio_pipeline_unregister(tone_pipeline, tone_source);
    audio_pipeline_unregister(tone_pipeline, mp3_decoder);
    audio_pipeline_unregister(tone_pipeline, resample);
    audio_pipeline_unregister(tone_pipeline, i2s_writer);
    audio_pipeline_deinit(tone_pipeline);
    audio_element_deinit(tone_source);
    audio_element_deinit(mp3_decoder);
    audio_element_deinit(resample);
    audio_element_deinit(i2s_writer);
    audio_event_iface_destroy(evt);

    // Resume main playback ONLY if not recording (waiting for AI response)
    if (pipeline_play && !is_recording) {
        ESP_LOGI(TAG, "Resuming playback pipeline...");
        audio_pipeline_resume(pipeline_play);
    }
}

static void tone_task(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(s_play_tone_sem, portMAX_DELAY) == pdTRUE) {
            play_tone_logic();
        }
    }
}

static void play_tone(const char *uri) {
    if (s_play_tone_sem) {
        xSemaphoreGive(s_play_tone_sem);
    }
}


/**
 * @brief WebSocket event handler for audio streaming
 * @param handler_args User argument (unused)
 * @param base Event base
 * @param event_id Event ID
 * @param event_data Event data
 */
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket Connected");
        break;
        
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket Disconnected");
        // Stop recording if disconnected mid-stream
        if (is_recording) {
            ESP_LOGW(TAG, "Recording interrupted by disconnect");
            is_recording = false;
        }
        break;
        
    case WEBSOCKET_EVENT_DATA:
        if (!data || data->data_len == 0) break;
        
        if (data->op_code == 0x01) { // Text frame
            char *text = strndup((char *)data->data_ptr, data->data_len);
            if (text) {
                if (strcmp(text, "AUDIO_END") == 0) {
                    ESP_LOGW(TAG, "=== AI RESPONSE COMPLETE ===");
                    ESP_LOGI(TAG, "Ready for next Wake Word");
                    ai_response_complete = true;  // Signal waiting task
                } else if (strcmp(text, "AUDIO_START") == 0) {
                    ESP_LOGI(TAG, "=== AUDIO START ===");
                    flush_audio_flag = false; // Stop flushing, accept new audio
                    
                    // Ensure playback pipeline is running for response
                    if (pipeline_play) {
                        audio_pipeline_resume(pipeline_play);
                    }
                } else if (strcmp(text, "STOP_RECORDING") == 0) {
                    ESP_LOGW(TAG, "🛑 Server requested STOP (silence detected)");
                    is_recording = false;  // Stop the streaming task
                }
                free(text);
            }
        } 
        else if (data->op_code == 0x02) { // Binary frame - PCM data
            if (flush_audio_flag) {
                ESP_LOGD(TAG, "Dropping stale audio packet (%d bytes)", data->data_len);
                break; // Ignore this packet
            }
            ESP_LOGD(TAG, "RX Binary: %d bytes", data->data_len);
            // Write directly to playback pipeline with retry logic
            if (raw_write_el) {
                int written = 0;
                int remaining = data->data_len;
                char *ptr = (char *)data->data_ptr;
                int64_t start_time = esp_timer_get_time();
                int retry_delay = 10; // Start with 10ms
                
                while (remaining > 0) {
                    // Try to write data
                    written = raw_stream_write(raw_write_el, ptr, remaining);
                    
                    if (written > 0) {
                        // Successfully wrote some data
                        ptr += written;
                        remaining -= written;
                        retry_delay = 10; // Reset delay on success
                        
                        if (remaining > 0) {
                            ESP_LOGD(TAG, "Partial write: %d/%d bytes, retrying...", data->data_len - remaining, data->data_len);
                        }
                    } else {
                        // Write failed - check if we should retry
                        int64_t elapsed_ms = (esp_timer_get_time() - start_time) / 1000;
                        
                        if (elapsed_ms > AUDIO_WRITE_TIMEOUT_MS) {
                            ESP_LOGW(TAG, "Write timeout after %lld ms, dropped %d bytes", elapsed_ms, remaining);
                            break;
                        }
                        
                        // Backpressure: wait and retry with exponential backoff
                        ESP_LOGD(TAG, "Buffer full, waiting %d ms...", retry_delay);
                        vTaskDelay(pdMS_TO_TICKS(retry_delay));
                        
                        // Exponential backoff (10, 20, 40, 80, 100 max)
                        retry_delay = (retry_delay < 100) ? retry_delay * 2 : 100;
                    }
                }
                
                if (remaining == 0) {
                    ESP_LOGD(TAG, "Wrote %d bytes to playback pipeline", data->data_len);
                } else {
                    ESP_LOGW(TAG, "Wrote %d/%d bytes (dropped %d)", 
                             data->data_len - remaining, data->data_len, remaining);
                }
            }
        }
        break;
        
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket Error");
        break;
        
    default:
        break;
    }
}

/**
 * @brief Task to stream audio to WebSocket server  
 * @param pvParameters Task parameters (unused)
 * @note This task self-deletes after completion
 * 
 * SIMPLIFIED LOGIC (Server-side VAD):
 * 1. After wake word, stream ALL audio for a fixed duration
 * 2. NO client-side VAD - server will use Silero VAD to trim silence
 * 3. Simpler, more reliable, and lets server do the smart processing
 */
static void websocket_stream_task(void *pvParameters) {
    ESP_LOGI(TAG, "🎙️ Starting Audio Stream (Server-side VAD)...");
    
    // Allocate audio buffer for current chunk
    char *buffer = malloc(AUDIO_CHUNK_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "❌ Failed to allocate audio buffer");
        is_recording = false;
        stream_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // --- Phase 1: Connect to WebSocket ---
    if (!esp_websocket_client_is_connected(ws_client)) {
        ESP_LOGW(TAG, "🔌 WebSocket not connected, reconnecting...");
        esp_websocket_client_start(ws_client);
        
        int retry = 0;
        int retry_delay = WS_RETRY_DELAY_MS;
        
        while (!esp_websocket_client_is_connected(ws_client) && retry < WS_RETRY_MAX) {
            ESP_LOGW(TAG, "⏳ Waiting for WebSocket... (%d/%d)", retry + 1, WS_RETRY_MAX);
            vTaskDelay(pdMS_TO_TICKS(retry_delay));
            
            retry_delay = (retry_delay * 2 > WS_MAX_RETRY_DELAY_MS) ? WS_MAX_RETRY_DELAY_MS : retry_delay * 2;
            retry++;
        }
    }
    
    if (!esp_websocket_client_is_connected(ws_client)) {
        ESP_LOGE(TAG, "❌ WebSocket connection failed! Aborting.");
        free(buffer);
        is_recording = false;
        stream_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "✅ WebSocket ready");
    
    // --- Phase 2: Stream Audio for Fixed Duration ---
    ESP_LOGI(TAG, "📤 Streaming audio (timeout: %d ms)...", VAD_WAIT_TIMEOUT_MS);
    
    int64_t start_time = esp_timer_get_time();
    int total_sent = 0;
    int read_len = 0;
    int empty_reads = 0;
    const int MAX_EMPTY_READS = 20; // Stop after 20 consecutive empty reads (~2 seconds)
    
    while (is_recording) {
        // Check timeout
        int64_t elapsed_ms = (esp_timer_get_time() - start_time) / 1000;
        if (elapsed_ms > VAD_WAIT_TIMEOUT_MS) {
            ESP_LOGI(TAG, "⏱️ Streaming timeout reached (%lld ms)", elapsed_ms);
            break;
        }
        
        // Read audio chunk
        read_len = audio_recorder_data_read(recorder, buffer, AUDIO_CHUNK_SIZE, pdMS_TO_TICKS(100));
        
        if (read_len > 0) {
            // Yield to prevent watchdog timeout (allow IDLE task to run)
            vTaskDelay(pdMS_TO_TICKS(10));
            
            // Reset empty read counter
            empty_reads = 0;
            
            // Send audio chunk to server
            if (esp_websocket_client_send_bin(ws_client, buffer, read_len, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS)) < 0) {
                ESP_LOGE(TAG, "❌ Failed to send audio chunk");
                break;
            }
            
            total_sent += read_len;
            
            // Log progress periodically
            if (total_sent % PROGRESS_LOG_INTERVAL < AUDIO_CHUNK_SIZE) {
                ESP_LOGI(TAG, "📊 Sent: %d bytes (%.1fs)", total_sent, (float)elapsed_ms / 1000);
            }
            
        } else {
            // No data from recorder
            empty_reads++;
            
            if (empty_reads >= MAX_EMPTY_READS) {
                ESP_LOGI(TAG, "✅ No more audio data (%d empty reads)", empty_reads);
                break;
            }
            
            // Small delay before next read
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    // --- Phase 3: Finalize ---
    free(buffer);
    
    float duration_sec = (float)total_sent / (REC_SAMPLE_RATE * 2);
    ESP_LOGI(TAG, "📊 Total sent: %d bytes (%.2fs at 16kHz)", total_sent, duration_sec);
    
    // Send END marker
    esp_websocket_client_send_text(ws_client, "END", 3, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
    ESP_LOGI(TAG, "📤 END signal sent");
    
    // Wait for AI response
    ESP_LOGI(TAG, "⏳ Waiting for AI response...");
    
    int64_t wait_start = esp_timer_get_time();
    bool timeout_occurred = false;
    
    while (!ai_response_complete) {
        int64_t elapsed_ms = (esp_timer_get_time() - wait_start) / 1000;
        
        if (elapsed_ms > AI_RESPONSE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "⏱️ AI response timeout after %lld ms", elapsed_ms);
            timeout_occurred = true;
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    if (!timeout_occurred) {
        ESP_LOGI(TAG, "✅ AI response received successfully");
    }
    
    is_recording = false;
    stream_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Input callback for AFE (Audio Front End)
 * @param buffer Buffer to fill with audio data
 * @param buf_sz Buffer size in bytes
 * @param user_ctx User context (unused)
 * @param ticks Timeout in ticks
 * @return Number of bytes read
 */
static int input_cb_for_afe(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks) {
    return raw_stream_read(raw_read_el, (char *)buffer, buf_sz);
}

/**
 * @brief Recorder event callback - handles wake word detection
 * @param event Event data
 * @param user_data User data (unused)
 * @return ESP_OK on success
 */
static esp_err_t recorder_event_cb(audio_rec_evt_t *event, void *user_data) {
    if (AUDIO_REC_WAKEUP_START == event->type) {
        ESP_LOGI(TAG, "🔔 ═══════════════════════════════════════════════");
        ESP_LOGI(TAG, "🔔 WAKE WORD DETECTED!");
        ESP_LOGI(TAG, "🔔 ═══════════════════════════════════════════════");
        
        if (!is_recording) {
            // Reset response flag IMMEDIATELY to avoid race condition with delayed AUDIO_END
            ESP_LOGI(TAG, "🔄 Resetting ai_response_complete flag (was: %d)", ai_response_complete);
            ai_response_complete = false;
            
            ESP_LOGI(TAG, "🚫 Setting flush_audio_flag to drop stale packets");
            flush_audio_flag = true; // Start flushing stale audio from previous turn
            
            // --- BARGE-IN: Stop Playback Immediately ---
            if (pipeline_play) {
                ESP_LOGI(TAG, "🛑 Barge-in: Pausing playback pipeline");
                audio_pipeline_pause(pipeline_play);
                if (raw_write_el) {
                    audio_element_reset_output_ringbuf(raw_write_el);
                    audio_element_reset_state(raw_write_el);
                }
                if (i2s_writer_el) {
                    audio_element_reset_state(i2s_writer_el); // Clear DMA buffers
                }
            }
            
            // Notify Server to stop streaming
            if (esp_websocket_client_is_connected(ws_client)) {
                esp_websocket_client_send_text(ws_client, "BARGE_IN", 8, pdMS_TO_TICKS(500));
                ESP_LOGI(TAG, "📤 Sent BARGE_IN signal to server");
            }
            // -------------------------------------------

            ESP_LOGI(TAG, "🔔 Playing wake sound...");
            play_tone("flash://tone/0_dingdong.mp3");
            
            ESP_LOGI(TAG, "🎙️ Starting audio stream task...");
            is_recording = true;
            xTaskCreate(websocket_stream_task, "ws_stream", STREAM_TASK_STACK_SIZE, NULL, STREAM_TASK_PRIORITY, &stream_task_handle);
        } else {
            ESP_LOGW(TAG, "⚠️ Wake word detected but already recording - ignoring");
        }
    } else if (AUDIO_REC_WAKEUP_END == event->type) {
        ESP_LOGI(TAG, "🔚 Wake Word Session Ended");
        is_recording = false;
    }
    return ESP_OK;
}

// --- Button Handling with Input Key Service ---
static esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    audio_board_handle_t board_handle = (audio_board_handle_t) ctx;
    int player_volume = 0;
    audio_hal_get_volume(board_handle->audio_hal, &player_volume);
    
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK) {
        ESP_LOGI(TAG, "[ * ] Button Click - ID:%d", (int)evt->data);
        
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_VOLUP:
                ESP_LOGI(TAG, "[ * ] [Vol+] Click");
                player_volume += 10;
                if (player_volume > 100) {
                    player_volume = 100;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                settings_set_volume(player_volume);  // Save to NVS
                ESP_LOGI(TAG, "[ * ] Volume set to %d (saved)", player_volume);
                break;
                
            case INPUT_KEY_USER_ID_VOLDOWN:
                ESP_LOGI(TAG, "[ * ] [Vol-] Click");
                player_volume -= 10;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                settings_set_volume(player_volume);  // Save to NVS
                ESP_LOGI(TAG, "[ * ] Volume set to %d (saved)", player_volume);
                break;
                
            default:
                ESP_LOGI(TAG, "[ * ] Button %d not mapped", (int)evt->data);
                break;
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Main application entry point
 */
void app_main(void) {
    // Configure logging (Reduced for performance)
    esp_log_level_set("*", ESP_LOG_ERROR);  // Only critical errors
    esp_log_level_set(TAG, ESP_LOG_WARN);   // Important warnings for main app

    ESP_LOGI(TAG, "=== ESP32-LyraT-Mini WebSocket Audio Streaming ===");

    // Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize Settings Module
    ESP_LOGI(TAG, "Initializing settings...");
    ret = settings_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize settings: %s", esp_err_to_name(ret));
    }
    
    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    wifi_init_sta(WIFI_SSID, WIFI_PASS, WIFI_RETRY_COUNT);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Initialize Audio Board & Codec
    ESP_LOGI(TAG, "Initializing Audio Board...");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    
    // Load volume from settings
    int saved_volume = settings_get_volume();
    audio_hal_set_volume(board_handle->audio_hal, saved_volume);
    ESP_LOGI(TAG, "Volume restored from settings: %d", saved_volume);
    // Explicitly set codec sample rate to match playback stream
    // audio_hal_set_sample_rate(board_handle->audio_hal, PLAY_SAMPLE_RATE); // Removed: Not supported in this ADF version

    // Initialize tone playback pipeline (persistent)
    // init_tone_pipeline(); // Removed: Tone pipeline is now ephemeral in play_tone

    // --- Initialize Tone Task ---
    s_play_tone_sem = xSemaphoreCreateBinary();
    xTaskCreate(tone_task, "tone_task", 8192, NULL, 5, NULL);

    // --- Initialize Playback Pipeline (Persistent) ---
    init_play_pipeline();
    // -------------------------------------------------

    // Initialize WebSocket Client
    ESP_LOGI(TAG, "Connecting to WebSocket: %s", WS_URI);
    esp_websocket_client_config_t websocket_cfg = {
        .uri = WS_URI,
        .buffer_size = WS_BUFFER_SIZE,
        .keep_alive_enable = true,
        .ping_interval_sec = WS_PING_INTERVAL_SEC,
        .disable_auto_reconnect = false,
        .disable_pingpong_discon = true, // Prevent disconnect on missed pong
    };
    
    ws_client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    esp_websocket_client_start(ws_client);
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for connection

    // Recorder Pipeline
    audio_pipeline_handle_t pipeline_rec;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_rec = audio_pipeline_init(&pipeline_cfg);

    i2s_stream_cfg_t i2s_rec_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_rec_cfg.type = AUDIO_STREAM_READER;
    i2s_rec_cfg.chan_cfg.id = I2S_NUM_REC;
    i2s_rec_cfg.std_cfg.clk_cfg.sample_rate_hz = REC_SAMPLE_RATE;
    i2s_rec_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO; 
    i2s_rec_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    audio_element_handle_t i2s_reader = i2s_stream_init(&i2s_rec_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_cfg.out_rb_size = RAW_READ_BUFFER_SIZE;
    raw_read_el = raw_stream_init(&raw_cfg);

    audio_pipeline_register(pipeline_rec, i2s_reader, "i2s");
    audio_pipeline_register(pipeline_rec, raw_read_el, "raw");
    audio_pipeline_link(pipeline_rec, (const char *[]) {"i2s", "raw"}, 2);
    audio_pipeline_run(pipeline_rec);

    // Recorder SR (WakeNet)
    ESP_LOGI(TAG, "Initializing WakeNet...");
    recorder_sr_cfg_t recorder_sr_cfg = DEFAULT_RECORDER_SR_CFG(
        "LM", 
        "model", 
        AFE_TYPE_SR, 
        AFE_MODE_HIGH_PERF
    );
    recorder_sr_cfg.afe_cfg->wakenet_init = true;
    recorder_sr_cfg.afe_cfg->vad_mode = VAD_MODE_4;
    recorder_sr_cfg.afe_cfg->afe_linear_gain = 1.0;
    
    // --- AEC Disabled (Hardware Limitation) ---
    // recorder_sr_cfg.afe_cfg->aec_init = true;
    // recorder_sr_cfg.afe_cfg->se_init = true;
    // ------------------------------------------
    
    audio_rec_cfg_t rec_cfg = AUDIO_RECORDER_DEFAULT_CFG();
    rec_cfg.task_prio = RECORDER_TASK_PRIORITY;
    rec_cfg.task_size = 12 * 1024; // Increase stack size for recorder task
    rec_cfg.read = (recorder_data_read_t)&input_cb_for_afe;
    rec_cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &rec_cfg.sr_iface);
    rec_cfg.event_cb = recorder_event_cb;
    
    recorder = audio_recorder_create(&rec_cfg);

    ESP_LOGI(TAG, "System Ready! Say 'Jarvis'!");
    ESP_LOGI(TAG, "Free Heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // --- Button & Input Key Service Init ---
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    
    // Initialize audio board keys (buttons)
    audio_board_key_init(set);
    
    // Start input key service for button handling
    input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_cfg_t input_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    input_cfg.handle = set;
    
    periph_service_handle_t input_ser = input_key_service_create(&input_cfg);
    input_key_service_add_key(input_ser, input_key_info, INPUT_KEY_NUM);
    periph_service_set_callback(input_ser, input_key_service_cb, (void *)board_handle);
    
    ESP_LOGI(TAG, "Button input service started");
    // --------------------------------

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

