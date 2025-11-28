/*
 * ESP32-LyraT-Mini V1.2 Wake Word & Audio Streaming với WebSocket + SD MP3 Player
 * 
 * Features:
 * 1. Wake Word Detection ("Jarvis") using ESP-SR WakeNet
 * 2. Real-time Audio Streaming to server via WebSocket
 * 3. MP3 response playback từ server
 * 4. MP3 Player from SD Card (MODE button to toggle)
 * 
 * Controls:
 * - MODE: Toggle Wake Word ↔ MP3 Player
 * - PLAY: Play/Pause (MP3 mode)
 * - REC: Next track (MP3 mode)
 * - VOL+/VOL-: Volume control
 */

#include <string.h>
#include <dirent.h>
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
#include "periph_sdcard.h"
#include "input_key_service.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "fatfs_stream.h"
#include "audio_recorder.h"
#include "recorder_sr.h"
#include "esp_websocket_client.h"
#include "wifi_helper.h"
#include "tone_stream.h"
#include "mp3_decoder.h"
#include <math.h>
#include "esp_timer.h"
#include "ringbuf.h"
#include "config.h"

// Bluetooth includes
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

static const char *TAG = "LYRAT_MINI_WS";

// ---// System mode enum
typedef enum {
    MODE_WAKE_WORD = 0,
    MODE_MP3_PLAYER = 1,
    MODE_BLUETOOTH = 2
} system_mode_t;

static system_mode_t current_mode = MODE_WAKE_WORD;

// --- Wake Word Mode State ---
static bool is_recording = false;
static bool ai_response_complete = false;  // Track AUDIO_END from server
static audio_rec_handle_t recorder = NULL;
static audio_element_handle_t raw_read_el = NULL;
static esp_websocket_client_handle_t ws_client = NULL;
static TaskHandle_t stream_task_handle = NULL;

static audio_pipeline_handle_t pipeline_play = NULL;
static audio_element_handle_t raw_write_el = NULL;
static audio_pipeline_handle_t pipeline_rec = NULL;  // Recording pipeline for wake word

// --- MP3 Player Mode State ---
static audio_pipeline_handle_t pipeline_mp3 = NULL;
static audio_element_handle_t fatfs_reader = NULL;
static audio_element_handle_t mp3_decoder = NULL;
static audio_element_handle_t i2s_writer_mp3 = NULL;
static audio_event_iface_handle_t evt_mp3 = NULL;

#define MP3_BASE_PATH "/sdcard"
#define MAX_PLAYLIST_SIZE 100

static char *playlist[MAX_PLAYLIST_SIZE];
static int playlist_count = 0;
static int current_track_index = 0;
static bool mp3_playing = false;

// --- Bluetooth Speaker Mode State ---
static audio_pipeline_handle_t pipeline_bt = NULL;
static audio_element_handle_t bt_stream_reader = NULL;
static audio_element_handle_t i2s_writer_bt = NULL;
static bool bt_connected = false;
static char bt_remote_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};

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

// ============================================================================
// MP3 PLAYER FUNCTIONS
// ============================================================================

/**
 * @brief Scan SD card for MP3 files and build playlist
 */
static void scan_mp3_files(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scanning SD card for MP3 files...");
    ESP_LOGI(TAG, "Directory: %s", MP3_BASE_PATH);
    ESP_LOGI(TAG, "========================================");
    
    // Clear existing playlist
    for (int i = 0; i < playlist_count; i++) {
        if (playlist[i]) {
            free(playlist[i]);
            playlist[i] = NULL;
        }
    }
    playlist_count = 0;
    
    DIR *dir = opendir(MP3_BASE_PATH);
    if (dir == NULL) {
        ESP_LOGE(TAG, "❌ Failed to open directory: %s", MP3_BASE_PATH);
        return;
    }
    
    struct dirent *entry;
    int total_entries = 0;
    
    while ((entry = readdir(dir)) != NULL && playlist_count < MAX_PLAYLIST_SIZE) {
        total_entries++;
        
        // Debug: Print ALL entries
        const char *type_str = (entry->d_type == DT_DIR) ? "DIR " : 
                               (entry->d_type == DT_REG) ? "FILE" : "????";
        
        ESP_LOGI(TAG, "[%d] %s: %s", total_entries, type_str, entry->d_name);
        
        // Skip hidden files and directories
        if (entry->d_name[0] == '.') {
            ESP_LOGD(TAG, "  → Skip: Hidden file/dir");
            continue;
        }
        
        // Skip directories (only process regular files)
        if (entry->d_type == DT_DIR) {
            ESP_LOGD(TAG, "  → Skip: Directory");
            continue;
        }
        
        // Get filename length
        int len = strlen(entry->d_name);
        
        // Check if file ends with .mp3 or .MP3 (minimum 5 chars: x.mp3)
        if (len < 5) {
            ESP_LOGD(TAG, "  → Skip: Filename too short");
            continue;
        }
        
        // Check extension (case insensitive)
        const char *ext = &entry->d_name[len - 4];
        if (strcasecmp(ext, ".mp3") == 0) {
            // Build full path
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", MP3_BASE_PATH, entry->d_name);
            
            // Add to playlist
            playlist[playlist_count] = strdup(full_path);
            if (playlist[playlist_count] == NULL) {
                ESP_LOGE(TAG, "  ❌ Failed to allocate memory for: %s", entry->d_name);
                continue;
            }
            
            ESP_LOGI(TAG, "  ✅ Added to playlist [%d]: %s", playlist_count, entry->d_name);
            playlist_count++;
        } else {
            ESP_LOGD(TAG, "  → Skip: Not .mp3 extension (ext: %s)", ext);
        }
    }
    closedir(dir);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scan complete:");
    ESP_LOGI(TAG, "  Total entries found: %d", total_entries);
    ESP_LOGI(TAG, "  MP3 files added: %d", playlist_count);
    ESP_LOGI(TAG, "========================================");
    
    if (playlist_count > 0) {
        ESP_LOGI(TAG, "✅ Playlist ready with %d song(s)", playlist_count);
        ESP_LOGI(TAG, "Playlist:");
        for (int i = 0; i < playlist_count; i++) {
            ESP_LOGI(TAG, "  [%d] %s", i, playlist[i]);
        }
    } else {
        ESP_LOGW(TAG, "⚠️  No MP3 files found in %s", MP3_BASE_PATH);
        ESP_LOGW(TAG, "Please check:");
        ESP_LOGW(TAG, "  1. SD card is inserted");
        ESP_LOGW(TAG, "  2. SD card is formatted (FAT32)");
        ESP_LOGW(TAG, "  3. MP3 files are in root directory");
    }
}

/**
 * @brief Initialize MP3 player pipeline
 */
static void init_mp3_pipeline(void) {
    ESP_LOGI(TAG, "Initializing MP3 Player Pipeline...");
    
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_mp3 = audio_pipeline_init(&pipeline_cfg);
    
    // FATFS Stream Reader (reads from SD card)
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.buf_sz = 32 * 1024;  // Larger buffer for smoother reading
    fatfs_cfg.task_prio = 6;       // Higher priority
    fatfs_reader = fatfs_stream_init(&fatfs_cfg);
    
    // MP3 Decoder
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_prio = 10;        // Very high priority for decoding
    mp3_cfg.task_stack = 8 * 1024; // Larger stack
    mp3_cfg.out_rb_size = 20 * 1024; // Large output buffer
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    
    // I2S Writer (for ES8311 speaker)
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = 32 * 1024;  // Very large buffer
    i2s_cfg.task_prio = 24;  // Maximum priority for I2S
    i2s_cfg.task_core = 1;   // Pin to core 1
    i2s_cfg.task_stack = 6144; // Larger stack
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 44100; // Default, will auto-adjust
    
    // DMA buffer configuration for smooth playback
    i2s_cfg.std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    i2s_cfg.std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    i2s_cfg.std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    
    // IMPORTANT: Use STEREO for MP3 (most MP3s are stereo)
    // ES8311 will automatically mix stereo to mono output
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    
    i2s_writer_mp3 = i2s_stream_init(&i2s_cfg);
    
    // Register elements
    audio_pipeline_register(pipeline_mp3, fatfs_reader, "file");
    audio_pipeline_register(pipeline_mp3, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline_mp3, i2s_writer_mp3, "i2s");
    
    // Link pipeline: SD Card -> MP3 Decoder -> I2S Speaker
    const char *link_tag[3] = {"file", "mp3", "i2s"};
    audio_pipeline_link(pipeline_mp3, &link_tag[0], 3);
    
    // Setup event listener
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt_mp3 = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline_mp3, evt_mp3);
    
    ESP_LOGI(TAG, "MP3 Pipeline ready");
}

/**
 * @brief Play MP3 file by index
 */
static void mp3_play_track(int index) {
    if (index < 0 || index >= playlist_count) {
        ESP_LOGW(TAG, "Invalid track index: %d", index);
        return;
    }
    
    if (playlist_count == 0) {
        ESP_LOGW(TAG, "No MP3 files in playlist");
        return;
    }
    
    current_track_index = index;
    
    ESP_LOGI(TAG, "Playing: %s", playlist[current_track_index]);
    
    // Stop current playback
    audio_pipeline_stop(pipeline_mp3);
    audio_pipeline_wait_for_stop(pipeline_mp3);
    audio_pipeline_reset_ringbuffer(pipeline_mp3);
    audio_pipeline_reset_elements(pipeline_mp3);
    
    // Set new URI
    audio_element_set_uri(fatfs_reader, playlist[current_track_index]);
    
    // Start playback
    audio_pipeline_run(pipeline_mp3);
    mp3_playing = true;
}

/**
 * @brief Stop MP3 playback
 */
static void mp3_stop(void) {
    if (pipeline_mp3) {
        audio_pipeline_stop(pipeline_mp3);
        audio_pipeline_wait_for_stop(pipeline_mp3);
        mp3_playing = false;
        ESP_LOGI(TAG, "MP3 stopped");
    }
}

/**
 * @brief Pause MP3 playback
 */
static void mp3_pause(void) {
    if (pipeline_mp3) {
        audio_pipeline_pause(pipeline_mp3);
        mp3_playing = false;
        ESP_LOGI(TAG, "MP3 paused");
    }
}

/**
 * @brief Resume MP3 playback
 */
static void mp3_resume(void) {
    if (pipeline_mp3) {
        audio_pipeline_resume(pipeline_mp3);
        mp3_playing = true;
        ESP_LOGI(TAG, "MP3 resumed");
    }
}

/**
 * @brief Play next track
 */
static void mp3_next_track(void) {
    if (playlist_count == 0) return;
    
    int next_index = (current_track_index + 1) % playlist_count;
    mp3_play_track(next_index);
}

/**
 * @brief Play previous track
 */
static void mp3_prev_track(void) {
    if (playlist_count == 0) return;
    
    int prev_index = (current_track_index - 1 + playlist_count) % playlist_count;
    mp3_play_track(prev_index);
}

/**
 * @brief Task to monitor MP3 playback events
 */
static void mp3_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "MP3 monitor task started");
    
    while (1) {
        if (current_mode == MODE_MP3_PLAYER && evt_mp3) {
            audio_event_iface_msg_t msg;
            esp_err_t ret = audio_event_iface_listen(evt_mp3, &msg, pdMS_TO_TICKS(100));
            
            if (ret == ESP_OK) {
                // MP3 music info - adjust I2S clock
                if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT 
                    && msg.source == (void *)mp3_decoder
                    && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                    
                    audio_element_info_t music_info = {0};
                    audio_element_getinfo(mp3_decoder, &music_info);
                    
                    ESP_LOGI(TAG, "🎵 MP3 Info: %d Hz, %d ch, %d bits", 
                             music_info.sample_rates, music_info.channels, music_info.bits);
                    
                    // Immediately adjust I2S clock for correct playback speed
                    if (music_info.sample_rates > 0) {
                        esp_err_t clk_ret = i2s_stream_set_clk(i2s_writer_mp3, 
                                                                music_info.sample_rates, 
                                                                music_info.bits, 
                                                                music_info.channels);
                        if (clk_ret == ESP_OK) {
                            ESP_LOGI(TAG, "✅ I2S clock set to %d Hz", music_info.sample_rates);
                        } else {
                            ESP_LOGW(TAG, "⚠️  Failed to set I2S clock: %s", esp_err_to_name(clk_ret));
                        }
                    }
                }
                
                // Track finished
                if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT 
                    && msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                    
                    audio_element_state_t el_state = audio_element_get_state((audio_element_handle_t)msg.source);
                    
                    if (el_state == AEL_STATE_FINISHED) {
                        ESP_LOGI(TAG, "⏭️  Track finished, playing next...");
                        vTaskDelay(pdMS_TO_TICKS(200)); // Small delay before next track
                        mp3_next_track();
                    } else if (el_state == AEL_STATE_ERROR) {
                        ESP_LOGE(TAG, "❌ Playback error, stopping...");
                        mp3_stop();
                    }
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Check more frequently for smoother operation
    }
}

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
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 16000; // Match server PCM output (16kHz)
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline_play, raw_write_el, "raw_write");
    audio_pipeline_register(pipeline_play, i2s_writer, "i2s_writer");
    audio_pipeline_link(pipeline_play, (const char *[]) {"raw_write", "i2s_writer"}, 2);
    
    audio_pipeline_run(pipeline_play);
}



static void play_tone(const char *uri) {
    if (!uri) return;
    ESP_LOGI(TAG, "Playing tone: %s", uri);

    // 1. Stop Playback Pipeline (Pause I2S)
    // We do NOT deinit, so raw_write_el remains valid for incoming WebSocket data.
    if (pipeline_play) {
        audio_pipeline_stop(pipeline_play);
        audio_pipeline_wait_for_stop(pipeline_play);
        // Do not reset ringbuffer here, we want to keep buffered audio!
    }

    // 2. Setup & Run Tone Pipeline (Ephemeral)
    audio_pipeline_handle_t tone_pipe = NULL;
    audio_pipeline_cfg_t pipe_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    tone_pipe = audio_pipeline_init(&pipe_cfg);

    tone_stream_cfg_t tone_cfg = TONE_STREAM_CFG_DEFAULT();
    tone_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t tone_reader = tone_stream_init(&tone_cfg);
    audio_element_set_uri(tone_reader, uri);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    audio_element_handle_t mp3_dec = mp3_decoder_init(&mp3_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = I2S_WRITE_BUFFER_SIZE;
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 48000; // Tone files are typically 48kHz 
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(tone_pipe, tone_reader, "tone");
    audio_pipeline_register(tone_pipe, mp3_dec, "mp3");
    audio_pipeline_register(tone_pipe, i2s_writer, "i2s");
    audio_pipeline_link(tone_pipe, (const char *[]) {"tone", "mp3", "i2s"}, 3);

    audio_pipeline_run(tone_pipe);

    // Wait for completion
    while (1) {
        audio_element_state_t state = audio_element_get_state(i2s_writer);
        if (state == AEL_STATE_FINISHED || state == AEL_STATE_ERROR) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Teardown Tone Pipeline
    audio_pipeline_stop(tone_pipe);
    audio_pipeline_wait_for_stop(tone_pipe);
    audio_pipeline_terminate(tone_pipe);
    
    audio_pipeline_unregister(tone_pipe, tone_reader);
    audio_pipeline_unregister(tone_pipe, mp3_dec);
    audio_pipeline_unregister(tone_pipe, i2s_writer);
    
    audio_element_deinit(tone_reader);
    audio_element_deinit(mp3_dec);
    audio_element_deinit(i2s_writer);
    
    audio_pipeline_deinit(tone_pipe);

    // 3. Resume Playback Pipeline
    if (pipeline_play) {
        ESP_LOGI(TAG, "Resuming playback pipeline...");
        
        // Reset the raw_write element's ringbuffer to clear any stale data
        audio_element_reset_state(raw_write_el);
        rb_reset(audio_element_get_output_ringbuf(raw_write_el));
        
        // Get I2S writer and reset its state too
        audio_element_handle_t i2s_writer = audio_pipeline_get_el_by_tag(pipeline_play, "i2s_writer");
        if (i2s_writer) {
            audio_element_reset_state(i2s_writer);
            rb_reset(audio_element_get_input_ringbuf(i2s_writer));
        }
        
        // Now restart the pipeline
        audio_pipeline_run(pipeline_play);
        
        ESP_LOGI(TAG, "Playback pipeline ready");
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
                }
                free(text);
            }
        } 
        else if (data->op_code == 0x02) { // Binary frame - PCM data
            ESP_LOGI(TAG, "RX Binary: %d bytes", data->data_len);
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
                    ESP_LOGI(TAG, "Wrote %d bytes to playback pipeline", data->data_len);
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
 */
/**
 * @brief Task to stream audio to WebSocket server
 * @param pvParameters Task parameters (unused)
 * @note This task self-deletes after completion
 */
static void websocket_stream_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting WebSocket Stream...");
    
    // Allocate audio buffer
    char *buffer = malloc(AUDIO_CHUNK_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        is_recording = false;
        stream_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Waiting for speech...");
    int64_t start_time = esp_timer_get_time();
    bool speech_detected = false;
    int read_len = 0;

    while (is_recording) {
        // Check timeout
        if ((esp_timer_get_time() - start_time) / 1000 > VAD_WAIT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Timeout waiting for speech");
            is_recording = false;
            break;
        }

        read_len = audio_recorder_data_read(recorder, buffer, AUDIO_CHUNK_SIZE, pdMS_TO_TICKS(100));
        if (read_len > 0) {
            float rms = calculate_rms((int16_t *)buffer, read_len);
            ESP_LOGI(TAG, "VAD Wait - RMS: %.2f", rms); // Debug log
            if (rms > VAD_RMS_THRESHOLD) {
                ESP_LOGI(TAG, "Speech detected! RMS: %.2f", rms);
                speech_detected = true;
                break; // Proceed to streaming
            }
            // Else: Silence (from AFE), skip this chunk
        } else {
            // VAD from library stopped
            is_recording = false;
            break;
        }
    }

    if (!speech_detected || !is_recording) {
        ESP_LOGW(TAG, "No speech detected or recording stopped");
        free(buffer);
        stream_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // --- Phase 2: Connect & Stream ---
    
    // Check connection and reconnect if needed
    if (!esp_websocket_client_is_connected(ws_client)) {
        ESP_LOGW(TAG, "WebSocket not connected, trying to reconnect...");
        esp_websocket_client_start(ws_client);
        
        int retry = 0;
        int retry_delay = WS_RETRY_DELAY_MS;
        
        while (!esp_websocket_client_is_connected(ws_client) && retry < WS_RETRY_MAX) {
            ESP_LOGW(TAG, "Waiting for WebSocket connection... (%d/%d)", retry + 1, WS_RETRY_MAX);
            vTaskDelay(pdMS_TO_TICKS(retry_delay));
            
            // Exponential backoff with cap
            retry_delay = retry_delay * 2;
            if (retry_delay > WS_MAX_RETRY_DELAY_MS) {
                retry_delay = WS_MAX_RETRY_DELAY_MS;
            }
            retry++;
        }
    }
    
    if (!esp_websocket_client_is_connected(ws_client)) {
        ESP_LOGE(TAG, "WebSocket still not connected! Aborting.");
        is_recording = false;
        free(buffer);
        stream_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "WebSocket ready, starting audio stream");
    
    // Send the first chunk (the one that triggered speech detection)
    if (esp_websocket_client_send_bin(ws_client, buffer, read_len, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS)) < 0) {
        ESP_LOGE(TAG, "Failed to send first audio chunk");
        is_recording = false;
        free(buffer);
        stream_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Stream remaining audio chunks
    int total_sent = read_len;
    
    while (is_recording) {
        read_len = audio_recorder_data_read(recorder, buffer, AUDIO_CHUNK_SIZE, pdMS_TO_TICKS(100));
        
        if (read_len > 0) {
            if (esp_websocket_client_send_bin(ws_client, buffer, read_len, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS)) < 0) {
                ESP_LOGE(TAG, "Failed to send audio chunk");
                break;
            }
            total_sent += read_len;
            
            // Log progress periodically
            if (total_sent % PROGRESS_LOG_INTERVAL < AUDIO_CHUNK_SIZE) {
                ESP_LOGI(TAG, "Sent %d bytes", total_sent);
            }
        } else {
            ESP_LOGI(TAG, "VAD detected silence");
            break;
        }
    }
    
    // Cleanup
    free(buffer);
    
    // Send END marker
    ESP_LOGI(TAG, "Total sent: %d bytes", total_sent);
    esp_websocket_client_send_text(ws_client, "END", 3, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
    
    ESP_LOGI(TAG, "Waiting for AI response...");
    
    // Wait for AUDIO_END with timeout (flag already reset in wake word callback)
    int64_t wait_start = esp_timer_get_time();
    bool timeout_occurred = false;
    
    while (!ai_response_complete) {
        int64_t elapsed_ms = (esp_timer_get_time() - wait_start) / 1000;
        
        if (elapsed_ms > AI_RESPONSE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "⏱️ AI response timeout after %lld ms - returning to ready state", elapsed_ms);
            timeout_occurred = true;
            break;
        }
        
        // Check every 500ms
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
        ESP_LOGI(TAG, "Wake Word Detected!");
        
        if (!is_recording) {
            // Reset response flag IMMEDIATELY to avoid race condition with delayed AUDIO_END
            ai_response_complete = false;
            
            play_tone("flash://tone/0_dingdong.mp3");
            
            is_recording = true;
            xTaskCreate(websocket_stream_task, "ws_stream", STREAM_TASK_STACK_SIZE, NULL, STREAM_TASK_PRIORITY, &stream_task_handle);
        }
    } else if (AUDIO_REC_WAKEUP_END == event->type) {
        ESP_LOGI(TAG, "Wake Word Session Ended");
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
            case INPUT_KEY_USER_ID_MODE:
                ESP_LOGI(TAG, "[ * ] [MODE] Click - Toggle Mode");
                
#if (ENABLE_WAKE_WORD_MODE && ENABLE_MP3_PLAYER_MODE)
                // Toggle mode - only available when both modes enabled
                if (current_mode == MODE_WAKE_WORD) {
                    ESP_LOGI(TAG, "🎵 Switching to MP3 Player Mode");
                    current_mode = MODE_MP3_PLAYER;
                    
                    // Stop recorder if active
                    if (is_recording) {
                        is_recording = false;
                    }
                    
                    // CRITICAL: STOP recorder pipeline AND audio_recorder completely
                    // This frees AFE and I2S resources
                    // NOTE: Don't use terminate() as it can crash with AFE
                    // NOTE: Recorder uses audio_recorder callback, NOT pipeline listener
                    
                    // 1. Pause audio_recorder to stop AFE
                    if (recorder) {
                        ESP_LOGI(TAG, "Pausing audio recorder (AFE)...");
                        audio_recorder_trigger_stop(recorder);
                        
                        // Suppress AFE warnings (it's paused but still running)
                        esp_log_level_set("AFE", ESP_LOG_ERROR);
                    }
                    
                    // 2. Stop recorder pipeline
                    if (pipeline_rec) {
                        ESP_LOGI(TAG, "Stopping recorder pipeline...");
                        audio_pipeline_stop(pipeline_rec);
                        audio_pipeline_wait_for_stop(pipeline_rec);
                        audio_pipeline_reset_ringbuffer(pipeline_rec);
                        audio_pipeline_reset_elements(pipeline_rec);
                    }
                    
                    // Stop WebSocket to prevent auto-reconnect spam
                    if (ws_client) {
                        ESP_LOGI(TAG, "Stopping WebSocket client...");
                        esp_websocket_client_stop(ws_client);
                    }
                    
                    // Stop WebSocket playback pipeline
                    if (pipeline_play) {
                        audio_pipeline_stop(pipeline_play);
                        audio_pipeline_wait_for_stop(pipeline_play);
                    }
                    
                    // Small delay to ensure all resources freed
                    vTaskDelay(pdMS_TO_TICKS(100));
                    
                    // Auto-play first track if available
                    if (playlist_count > 0 && !mp3_playing) {
                        mp3_play_track(0);
                    } else if (playlist_count == 0) {
                        ESP_LOGW(TAG, "No MP3 files found on SD card");
                    }
                    
                } else {
                    ESP_LOGI(TAG, "🎤 Switching to Wake Word Mode");
                    current_mode = MODE_WAKE_WORD;
                    
                    // Stop MP3 if playing
                    if (mp3_playing) {
                        mp3_stop();
                    }
                    
                    // Small delay before restarting recorder
                    vTaskDelay(pdMS_TO_TICKS(100));
                    
                    // Restart recorder pipeline (was stopped and reset)
                    if (pipeline_rec) {
                        ESP_LOGI(TAG, "Restarting recorder pipeline...");
                        audio_pipeline_run(pipeline_rec);
                    }
                    
                    // Restart audio_recorder to resume AFE
                    if (recorder) {
                        ESP_LOGI(TAG, "Resuming audio recorder (AFE)...");
                        audio_recorder_trigger_start(recorder);
                    }
                    
                    // Restart WebSocket client
                    if (ws_client) {
                        ESP_LOGI(TAG, "Starting WebSocket client...");
                        esp_websocket_client_start(ws_client);
                    }
                                        // Resume WebSocket playback pipeline
                    if (pipeline_play) {
                        audio_pipeline_run(pipeline_play);
                    }
                }
#else
                ESP_LOGW(TAG, "Mode toggle disabled - only one mode enabled in config");
#endif
                break;
                
            case INPUT_KEY_USER_ID_PLAY:
                ESP_LOGI(TAG, "[ * ] [PLAY] Click");
                
                if (current_mode == MODE_MP3_PLAYER) {
                    if (mp3_playing) {
                        mp3_pause();
                        ESP_LOGI(TAG, "⏸️ Paused");
                    } else {
                        if (playlist_count > 0) {
                            mp3_resume();
                            ESP_LOGI(TAG, "▶️ Resumed");
                        } else {
                            ESP_LOGW(TAG, "No MP3 files to play");
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "PLAY button only works in MP3 Player mode");
                }
                break;
                
            case INPUT_KEY_USER_ID_REC:
                ESP_LOGI(TAG, "[ * ] [REC] Click");
                
                if (current_mode == MODE_MP3_PLAYER) {
                    ESP_LOGI(TAG, "⏭️ Next Track");
                    mp3_next_track();
                } else {
                    ESP_LOGI(TAG, "REC button: Next track (MP3 mode only)");
                }
                break;
                
            case INPUT_KEY_USER_ID_SET:
                ESP_LOGI(TAG, "[ * ] [SET] Click");
                
                if (current_mode == MODE_MP3_PLAYER) {
                    ESP_LOGI(TAG, "⏮️ Previous Track");
                    mp3_prev_track();
                } else {
                    ESP_LOGI(TAG, "SET button: Previous track (MP3 mode only)");
                }
                break;
            
            case INPUT_KEY_USER_ID_VOLUP:
                ESP_LOGI(TAG, "[ * ] [Vol+] Click");
                player_volume += 10;
                if (player_volume > 100) {
                    player_volume = 100;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d", player_volume);
                break;
                
            case INPUT_KEY_USER_ID_VOLDOWN:
                ESP_LOGI(TAG, "[ * ] [Vol-] Click");
                player_volume -= 10;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d", player_volume);
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
    // Configure logging
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "=== ESP32-LyraT-Mini WebSocket Audio Streaming ===");

    // Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    wifi_init_sta(WIFI_SSID, WIFI_PASS, WIFI_RETRY_COUNT);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // --- Audio Board Init ---
    ESP_LOGI(TAG, "Initializing Audio Board...");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, CODEC_VOLUME_PERCENT);

    // --- Conditional Initialization Based on Features ---
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Feature Configuration:");
    ESP_LOGI(TAG, "  Wake Word Mode: %s", ENABLE_WAKE_WORD_MODE ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "  MP3 Player Mode: %s", ENABLE_MP3_PLAYER_MODE ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "  Default Mode: %s", DEFAULT_STARTUP_MODE ? "MP3 Player" : "Wake Word");
    ESP_LOGI(TAG, "========================================");

#if ENABLE_MP3_PLAYER_MODE
    // --- Initialize SD Card (ESP-ADF Way) ---
    ESP_LOGI(TAG, "Initializing SD Card...");
    
    // Initialize peripherals set first
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    
    // Use ESP-ADF helper to mount SD card (auto-configures for LyraT Mini)
    esp_err_t sd_ret = audio_board_sdcard_init(set, SD_MODE_1_LINE);
    
    if (sd_ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ SD Card mounted successfully");
        
        // Initialize MP3 pipeline and scan files
        init_mp3_pipeline();
        scan_mp3_files();
        
        // Start MP3 monitor task with larger stack
        xTaskCreate(mp3_monitor_task, "mp3_monitor", 6144, NULL, 5, NULL);
    } else {
        ESP_LOGE(TAG, "❌ SD Card mount failed: %s", esp_err_to_name(sd_ret));
        ESP_LOGE(TAG, "MP3 Player disabled");
        ESP_LOGW(TAG, "Please check:");
        ESP_LOGW(TAG, "  1. SD card is inserted correctly");
        ESP_LOGW(TAG, "  2. SD card is formatted (FAT32)");
        ESP_LOGW(TAG, "  3. SD card is working properly");
    }
#else
    ESP_LOGI(TAG, "MP3 Player mode DISABLED (compile-time config)");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
#endif

#if ENABLE_WAKE_WORD_MODE
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
    };
    
    ws_client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    esp_websocket_client_start(ws_client);
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for connection

    // Recorder Pipeline (already declared as global)
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
    
    audio_rec_cfg_t rec_cfg = AUDIO_RECORDER_DEFAULT_CFG();
    rec_cfg.task_prio = RECORDER_TASK_PRIORITY;
    rec_cfg.read = (recorder_data_read_t)&input_cb_for_afe;
    rec_cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &rec_cfg.sr_iface);
    rec_cfg.event_cb = recorder_event_cb;
    
    recorder = audio_recorder_create(&rec_cfg);

    ESP_LOGI(TAG, "System Ready! Say 'Jarvis'!");
#else
    ESP_LOGI(TAG, "Wake Word mode DISABLED (compile-time config)");
    ESP_LOGI(TAG, "System Ready!");
#endif

    // --- Button & Input Key Service Init ---
    // Reuse the 'set' from SD card initialization
    
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
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "Controls:");
#if (ENABLE_WAKE_WORD_MODE && ENABLE_MP3_PLAYER_MODE)
    ESP_LOGI(TAG, "  MODE: Toggle Wake Word ↔ MP3 Player");
#endif
#if ENABLE_MP3_PLAYER_MODE
    ESP_LOGI(TAG, "  PLAY: Play/Pause%s", ENABLE_WAKE_WORD_MODE ? " (MP3 mode)" : "");
    ESP_LOGI(TAG, "  REC:  Next Track%s", ENABLE_WAKE_WORD_MODE ? " (MP3 mode)" : "");
    ESP_LOGI(TAG, "  SET:  Previous Track%s", ENABLE_WAKE_WORD_MODE ? " (MP3 mode)" : "");
#endif
    ESP_LOGI(TAG, "  VOL+/VOL-: Volume Control");
    ESP_LOGI(TAG, "==============================================");
    // --------------------------------

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

