/*
 * ESP32-LyraT-Mini V1.2 Wake Word & Audio Streaming
 * 
 * Features:
 * 1. Wake Word Detection ("Jarvis") using ESP-SR.
 * 2. Audio Recording from ES7243 Microphone (I2S1).
 * 3. Audio Streaming to Python Server via HTTP POST (Chunked).
 * 4. Audio Playback (Feedback Tone) via ES8311 (I2S0).
 * 
 * Hardware: ESP32-LyraT-Mini V1.2
 * - Mic: ES7243 (I2S1)
 * - Speaker: ES8311 (I2S0)
 * - Buttons: ADC
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_button.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "filter_resample.h"
#include "audio_recorder.h"
#include "recorder_sr.h"
#include "esp_http_client.h"
#include "wifi_helper.h"
#include "tone_stream.h"
#include "mp3_decoder.h"
#include "http_stream.h"
#include "cJSON.h"

static const char *TAG = "LYRAT_MINI_WWE";

// --- Configuration ---
#define WIFI_SSID           "Nguyen Van Hai"
#define WIFI_PASS           "0964822864"
#define SERVER_URL          "http://laihieu2714.ddns.net:6666/upload_audio"

// Hardware Definitions (LyraT-Mini V1.2)
#define I2S_NUM_PLAY        (0)
#define I2S_NUM_REC         (1)

// Recording Config
#define REC_SAMPLE_RATE     (16000)
#define REC_BITS            (16)
#define REC_CHANNELS        (1)
#define REC_TIME_SEC        (10)

// State Management
static bool is_recording = false;
// static audio_pipeline_handle_t pipeline_play = NULL; // Unused
static audio_rec_handle_t recorder = NULL;
static audio_element_handle_t raw_read_el = NULL;

// --- WAV Header Helper ---
typedef struct {
    char riff_header[4]; 
    int wav_size; 
    char wave_header[4]; 
    char fmt_header[4]; 
    int fmt_chunk_size; 
    short audio_format; 
    short num_channels; 
    int sample_rate; 
    int byte_rate; 
    short sample_alignment; 
    short bit_depth; 
    char data_header[4]; 
    int data_bytes; 
} wav_header_t;

static void generate_wav_header(wav_header_t *header, int sample_rate, int bits, int channels, int data_len) {
    memcpy(header->riff_header, "RIFF", 4);
    header->wav_size = data_len + 36;
    memcpy(header->wave_header, "WAVE", 4);
    memcpy(header->fmt_header, "fmt ", 4);
    header->fmt_chunk_size = 16;
    header->audio_format = 1; // PCM
    header->num_channels = channels;
    header->sample_rate = sample_rate;
    header->byte_rate = sample_rate * channels * (bits / 8);
    header->sample_alignment = channels * (bits / 8);
    header->bit_depth = bits;
    memcpy(header->data_header, "data", 4);
    header->data_bytes = data_len;
}

// --- Playback (Tone) ---
static void play_tone(const char *uri) {
    ESP_LOGI(TAG, "Playing tone: %s", uri);
    
    // Simple pipeline: Tone -> MP3 -> I2S
    audio_pipeline_handle_t pipeline;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    tone_stream_cfg_t tone_cfg = TONE_STREAM_CFG_DEFAULT();
    tone_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t tone_reader = tone_stream_init(&tone_cfg);
    audio_element_set_uri(tone_reader, uri);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    audio_element_handle_t mp3_dec = mp3_decoder_init(&mp3_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = 8 * 1024;
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 48000; // Default, will auto-adjust
    audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline, tone_reader, "tone");
    audio_pipeline_register(pipeline, mp3_dec, "mp3");
    audio_pipeline_register(pipeline, i2s_writer, "i2s");
    audio_pipeline_link(pipeline, (const char *[]) {"tone", "mp3", "i2s"}, 3);

    audio_pipeline_run(pipeline);

    // Wait for finish
    while (1) {
        audio_element_state_t state = audio_element_get_state(i2s_writer);
        if (state == AEL_STATE_FINISHED || state == AEL_STATE_ERROR) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);
    audio_pipeline_unregister(pipeline, tone_reader);
    audio_pipeline_unregister(pipeline, mp3_dec);
    audio_pipeline_unregister(pipeline, i2s_writer);
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(tone_reader);
    audio_element_deinit(mp3_dec);
    audio_element_deinit(i2s_writer);
}

// --- Playback (HTTP Stream) ---
static void play_audio_from_url(const char *url) {
    ESP_LOGI(TAG, "Playing audio from URL: %s", url);
    
    audio_pipeline_handle_t pipeline;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.type = AUDIO_STREAM_READER;
    audio_element_handle_t http_stream_reader = http_stream_init(&http_cfg);
    audio_element_set_uri(http_stream_reader, url);

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    audio_element_handle_t mp3_dec = mp3_decoder_init(&mp3_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = 8 * 1024;
    i2s_cfg.chan_cfg.id = I2S_NUM_PLAY;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = 24000; // Edge TTS rate
    audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_dec, "mp3");
    audio_pipeline_register(pipeline, i2s_writer, "i2s");
    audio_pipeline_link(pipeline, (const char *[]) {"http", "mp3", "i2s"}, 3);

    // Setup event listener to get audio info and adjust I2S clock
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    audio_pipeline_run(pipeline);

    // Wait for audio info from MP3 decoder and set I2S clock accordingly
    bool clock_set = false;
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, pdMS_TO_TICKS(100));
        
        if (ret == ESP_OK) {
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT 
                && msg.source == (void *)mp3_dec
                && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(mp3_dec, &music_info);
                
                ESP_LOGI(TAG, "MP3 Info: rate=%d, ch=%d, bits=%d", 
                         music_info.sample_rates, music_info.channels, music_info.bits);
                
                if (!clock_set && music_info.sample_rates > 0) {
                    i2s_stream_set_clk(i2s_writer, music_info.sample_rates, music_info.bits, music_info.channels);
                    clock_set = true;
                    ESP_LOGI(TAG, "I2S clock set to %d Hz", music_info.sample_rates);
                }
            }
        }
        
        audio_element_state_t state = audio_element_get_state(i2s_writer);
        if (state == AEL_STATE_FINISHED || state == AEL_STATE_ERROR) break;
    }

    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);
    
    audio_event_iface_destroy(evt);
    
    audio_pipeline_unregister(pipeline, http_stream_reader);
    audio_pipeline_unregister(pipeline, mp3_dec);
    audio_pipeline_unregister(pipeline, i2s_writer);
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(mp3_dec);
    audio_element_deinit(i2s_writer);
}

// --- HTTP Streaming Task ---
static void http_stream_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting HTTP Stream Task...");
    
    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000, // Increased timeout for Server Processing (Whisper + Gemini + TTS)
        .buffer_size = 8192, // Increased internal HTTP buffer
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        is_recording = false;
        vTaskDelete(NULL);
        return;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    
    // Allocate buffer for audio data (max 10 seconds)
    int max_recording_seconds = REC_TIME_SEC;
    int max_audio_bytes = max_recording_seconds * REC_SAMPLE_RATE * (REC_BITS / 8) * REC_CHANNELS;
    char *audio_buffer = malloc(max_audio_bytes);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        esp_http_client_cleanup(client);
        is_recording = false;
        vTaskDelete(NULL);
        return;
    }
    
    // Read all audio data into buffer
    int total_audio_bytes = 0;
    char *read_buffer = malloc(4096);
    
    ESP_LOGI(TAG, "Recording audio...");
    while (is_recording && total_audio_bytes < max_audio_bytes) {
        int read_len = audio_recorder_data_read(recorder, read_buffer, 4096, pdMS_TO_TICKS(100));
        
        if (read_len > 0) {
            // Copy to audio buffer
            int copy_len = (total_audio_bytes + read_len > max_audio_bytes) 
                          ? (max_audio_bytes - total_audio_bytes) 
                          : read_len;
            memcpy(audio_buffer + total_audio_bytes, read_buffer, copy_len);
            total_audio_bytes += copy_len;
        } else if (read_len <= 0) {
            // VAD ended
            ESP_LOGI(TAG, "VAD detected silence, ending recording");
            break;
        }
    }
    
    free(read_buffer);
    ESP_LOGI(TAG, "Recorded %d bytes of audio data", total_audio_bytes);
    
    // Generate WAV header with actual size
    wav_header_t header;
    generate_wav_header(&header, REC_SAMPLE_RATE, REC_BITS, REC_CHANNELS, total_audio_bytes);
    
    // Open HTTP connection with exact Content-Length
    int content_length = sizeof(wav_header_t) + total_audio_bytes;
    esp_err_t err = esp_http_client_open(client, content_length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(audio_buffer);
        esp_http_client_cleanup(client);
        is_recording = false;
        vTaskDelete(NULL);
        return;
    }
    
    // Send WAV header
    esp_http_client_write(client, (const char *)&header, sizeof(header));
    
    // Send audio data
    int bytes_sent = 0;
    while (bytes_sent < total_audio_bytes) {
        int chunk_size = (total_audio_bytes - bytes_sent > 4096) ? 4096 : (total_audio_bytes - bytes_sent);
        int write_len = esp_http_client_write(client, audio_buffer + bytes_sent, chunk_size);
        if (write_len < 0) {
            ESP_LOGE(TAG, "HTTP Write Error");
            break;
        }
        bytes_sent += write_len;
    }
    
    free(audio_buffer);
    ESP_LOGI(TAG, "Sent %d bytes (Header: %d + Audio: %d)", 
             sizeof(wav_header_t) + bytes_sent, sizeof(wav_header_t), bytes_sent);
    
    // Read Response
    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Fetch headers result: %d", content_length);
    
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
    
    if (content_length < 0) {
        ESP_LOGE(TAG, "HTTP client fetch headers failed (code: %d)", content_length);
    } else {
        int data_read = esp_http_client_read_response(client, buffer, 4096);
        if (data_read >= 0) {
            buffer[data_read] = 0; // Null-terminate
            ESP_LOGI(TAG, "HTTP Response: %s", buffer);
            
            // Parse JSON
            cJSON *root = cJSON_Parse(buffer);
            if (root) {
                cJSON *audio_url_json = cJSON_GetObjectItem(root, "audio_url");
                if (cJSON_IsString(audio_url_json) && (audio_url_json->valuestring != NULL)) {
                    ESP_LOGI(TAG, "Audio URL found: %s", audio_url_json->valuestring);
                    
                    // Play Audio
                    play_audio_from_url(audio_url_json->valuestring);
                }
                cJSON_Delete(root);
            }
        }
    }
    
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    is_recording = false;
    vTaskDelete(NULL);
}

// --- Recorder Callback ---
// Reverted to simple read as requested. 
// Note: With Stereo I2S, this feeds 2 channels to Mono AFE (2x speed).
// But this configuration was confirmed to detect the wake word.
static int input_cb_for_afe(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks) {
    return raw_stream_read(raw_read_el, (char *)buffer, buf_sz);
}

// Event Callback
static esp_err_t recorder_event_cb(audio_rec_evt_t *event, void *user_data) {
    if (AUDIO_REC_WAKEUP_START == event->type) {
        ESP_LOGI(TAG, "Wake Word Detected! (Jarvis)");
        
        if (!is_recording) {
            // 1. Play Feedback
            play_tone("flash://tone/0_dingdong.mp3");
            
            // 2. Start Streaming
            is_recording = true;
            xTaskCreate(http_stream_task, "http_stream", 8192, NULL, 5, NULL);
        } else {
            ESP_LOGW(TAG, "Already recording, ignoring wake word.");
        }
    } else if (AUDIO_REC_WAKEUP_END == event->type) {
        ESP_LOGI(TAG, "Wake Word Session Ended (Silence Detected)");
        is_recording = false; // Stop the stream task
    }
    return ESP_OK;
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    // 1. NVS & WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_init_sta(WIFI_SSID, WIFI_PASS, 5);
    
    // DISABLE POWER SAVE to prevent audio dropouts
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 2. Board & Codec Init
    ESP_LOGI(TAG, "Initializing Audio Board...");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, 80);

    // 3. Recorder Pipeline Init
    //    Flow: I2S_Stream (Reader) -> Raw_Stream -> [Callback] -> AFE (WakeNet) -> [Audio Recorder Read]
    
    audio_pipeline_handle_t pipeline_rec;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_rec = audio_pipeline_init(&pipeline_cfg);

    // I2S Reader (Mic) - ESP-IDF v5.x
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_cfg.chan_cfg.id = I2S_NUM_REC;
    i2s_cfg.std_cfg.clk_cfg.sample_rate_hz = REC_SAMPLE_RATE;
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO; 
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    audio_element_handle_t i2s_reader = i2s_stream_init(&i2s_cfg);

    // Raw Stream (Buffer for AFE)
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_cfg.out_rb_size = 64 * 1024; // Increased to 64KB to buffer more audio
    raw_read_el = raw_stream_init(&raw_cfg);

    audio_pipeline_register(pipeline_rec, i2s_reader, "i2s");
    audio_pipeline_register(pipeline_rec, raw_read_el, "raw");
    audio_pipeline_link(pipeline_rec, (const char *[]) {"i2s", "raw"}, 2);
    audio_pipeline_run(pipeline_rec);

    // 4. Recorder SR (WakeNet) Init
    ESP_LOGI(TAG, "Initializing WakeNet...");
    recorder_sr_cfg_t recorder_sr_cfg = DEFAULT_RECORDER_SR_CFG(
        "LM", 
        "model", 
        AFE_TYPE_SR, 
        AFE_MODE_HIGH_PERF
    );
    recorder_sr_cfg.afe_cfg->wakenet_init = true;
    recorder_sr_cfg.afe_cfg->vad_mode = VAD_MODE_4; // Aggressive VAD
    recorder_sr_cfg.afe_cfg->afe_linear_gain = 1.0; // Reduced gain to prevent noise from keeping VAD active
    
    audio_rec_cfg_t rec_cfg = AUDIO_RECORDER_DEFAULT_CFG();
    rec_cfg.read = (recorder_data_read_t)&input_cb_for_afe;
    rec_cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &rec_cfg.sr_iface);
    rec_cfg.event_cb = recorder_event_cb;
    
    recorder = audio_recorder_create(&rec_cfg);

    ESP_LOGI(TAG, "System Ready. Say 'Jarvis'!");

    // Main Loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Keep main task alive
    }
}
