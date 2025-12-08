/*
 * WiFi helper for ESP32-LyraT-Mini
 * Features:
 * - NVS storage for WiFi credentials
 * - Serial configuration via "wifi <ssid> <password>"
 * - Automatic reconnection with retry logic
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "driver/gpio.h"

static const char *TAG_WIFI = "WIFI";

// --- LED WiFi Status ---
#define LED_WIFI_STATUS 27  // GPIO 27

// --- NVS KEYS ---
#define NVS_NAMESPACE  "wifi_config"
#define NVS_SSID_KEY   "ssid"
#define NVS_PASS_KEY   "password"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static int s_max_retry = 10; 

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static bool wifi_connected = false;
static bool config_mode = false;

// Forward declarations
static void serial_config_task(void *pvParameters);

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_WIFI, "WiFi started, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG_WIFI, "Retry to connect to AP (%d/%d)", s_retry_num, s_max_retry);
        } else {
            ESP_LOGW(TAG_WIFI, "Failed to connect to WiFi after %d attempts", s_max_retry);
            ESP_LOGW(TAG_WIFI, "Entering config mode. Please enter: wifi <ssid> <password>");
            config_mode = true;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        wifi_connected = true;
        config_mode = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        // Bật LED báo WiFi connected
        gpio_set_level(LED_WIFI_STATUS, 1);
        ESP_LOGI(TAG_WIFI, "WiFi Status LED ON (GPIO 27)");
    }
}

// Load WiFi credentials from NVS
static bool load_wifi_credentials(char *ssid, char *password, size_t max_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "No WiFi credentials found in NVS");
        return false;
    }

    size_t ssid_len = max_len;
    size_t pass_len = max_len;
    
    err = nvs_get_str(nvs_handle, NVS_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_get_str(nvs_handle, NVS_PASS_KEY, password, &pass_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG_WIFI, "Loaded WiFi credentials from NVS: %s", ssid);
    return true;
}

// Save WiFi credentials to NVS
static bool save_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "Failed to open NVS");
        return false;
    }

    err = nvs_set_str(nvs_handle, NVS_SSID_KEY, ssid);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_set_str(nvs_handle, NVS_PASS_KEY, password);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG_WIFI, "WiFi credentials saved to NVS");
        return true;
    }
    return false;
}

// Serial config task
static void serial_config_task(void *pvParameters) {
    char line[128];
    int pos = 0;
    
    ESP_LOGI(TAG_WIFI, "Serial config task started");
    
    while (1) {
        // Only listen when in config mode
        if (!config_mode) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        if (c == '\n' || c == '\r') {
            if (pos > 0) {
                line[pos] = '\0';
                
                // Parse command: wifi <ssid> <password> or wifi "ssid" "password"
                if (strncmp(line, "wifi ", 5) == 0) {
                    char *input = line + 5;
                    char ssid[32] = {0};
                    char password[64] = {0};
                    
                    // Trim leading whitespace
                    while (*input == ' ') input++;
                    
                    // Parse SSID (quoted or unquoted)
                    if (*input == '"') {
                        // Quoted SSID
                        input++; // Skip opening quote
                        char *end_quote = strchr(input, '"');
                        
                        if (end_quote != NULL) {
                            int ssid_len = end_quote - input;
                            if (ssid_len > 0 && ssid_len < sizeof(ssid)) {
                                strncpy(ssid, input, ssid_len);
                                ssid[ssid_len] = '\0';
                                input = end_quote + 1; // Move past closing quote
                            } else {
                                ESP_LOGW(TAG_WIFI, "SSID too long or empty");
                                pos = 0;
                                continue;
                            }
                        } else {
                            ESP_LOGW(TAG_WIFI, "Missing closing quote for SSID");
                            pos = 0;
                            continue;
                        }
                    } else {
                        // Unquoted SSID - find next space or quote
                        char *delimiter = input;
                        while (*delimiter && *delimiter != ' ' && *delimiter != '"') {
                            delimiter++;
                        }
                        
                        int ssid_len = delimiter - input;
                        if (ssid_len > 0 && ssid_len < sizeof(ssid)) {
                            strncpy(ssid, input, ssid_len);
                            ssid[ssid_len] = '\0';
                            input = delimiter;
                        } else {
                            ESP_LOGW(TAG_WIFI, "SSID too long or empty");
                            pos = 0;
                            continue;
                        }
                    }
                    
                    // Skip whitespace between SSID and password
                    while (*input == ' ') input++;
                    
                    // Parse Password (quoted or unquoted)
                    if (*input == '"') {
                        // Quoted password
                        input++; // Skip opening quote
                        char *end_quote = strchr(input, '"');
                        
                        if (end_quote != NULL) {
                            int pass_len = end_quote - input;
                            if (pass_len > 0 && pass_len < sizeof(password)) {
                                strncpy(password, input, pass_len);
                                password[pass_len] = '\0';
                            } else {
                                ESP_LOGW(TAG_WIFI, "Password too long or empty");
                                pos = 0;
                                continue;
                            }
                        } else {
                            ESP_LOGW(TAG_WIFI, "Missing closing quote for password");
                            pos = 0;
                            continue;
                        }
                    } else {
                        // Unquoted password - take rest of line
                        if (strlen(input) > 0 && strlen(input) < sizeof(password)) {
                            strncpy(password, input, sizeof(password) - 1);
                        } else if (strlen(input) == 0) {
                            ESP_LOGW(TAG_WIFI, "Password is empty");
                            pos = 0;
                            continue;
                        } else {
                            ESP_LOGW(TAG_WIFI, "Password too long");
                            pos = 0;
                            continue;
                        }
                    }
                    
                    // Validate and save
                    if (strlen(ssid) > 0 && strlen(password) > 0) {
                        ESP_LOGI(TAG_WIFI, "Received WiFi config - SSID: %s", ssid);
                        
                        // Save to NVS
                        if (save_wifi_credentials(ssid, password)) {
                            ESP_LOGI(TAG_WIFI, "WiFi credentials saved. Restarting WiFi...");
                            
                            // Stop WiFi
                            esp_wifi_stop();
                            vTaskDelay(pdMS_TO_TICKS(500));
                            
                            // Reconfigure and start
                            wifi_config_t wifi_config = {0};
                            memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
                            memcpy(wifi_config.sta.password, password, strlen(password));
                            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                            
                            esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
                            s_retry_num = 0;
                            esp_wifi_start();
                            
                            ESP_LOGI(TAG_WIFI, "Connecting to WiFi...");
                        } else {
                            ESP_LOGE(TAG_WIFI, "Failed to save WiFi credentials");
                        }
                    } else {
                        ESP_LOGW(TAG_WIFI, "Invalid credentials. SSID or password is empty");
                    }
                } else {
                    ESP_LOGW(TAG_WIFI, "Unknown command. Use: wifi \"ssid\" \"password\" or wifi ssid password");
                }
                
                pos = 0;
            }
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = c;
        }
    }
}

void wifi_init_sta(const char *fallback_ssid, const char *fallback_pass, int max_retry)
{
    char ssid[32] = {0};
    char password[64] = {0};
    bool use_nvs = false;

    s_max_retry = max_retry;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_got_ip));

    // Initialize LED WiFi Status GPIO
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_WIFI_STATUS);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(LED_WIFI_STATUS, 0);  // LED off initially
    ESP_LOGI(TAG_WIFI, "WiFi Status LED initialized (GPIO 27)");

    // Start serial config task
    xTaskCreate(serial_config_task, "serial_config", 4096, NULL, 5, NULL);

    // Try to load credentials from NVS first
    if (load_wifi_credentials(ssid, password, sizeof(ssid))) {
        use_nvs = true;
        ESP_LOGI(TAG_WIFI, "Using WiFi credentials from NVS");
    } else {
        // Use fallback credentials if provided
        if (fallback_ssid && fallback_pass) {
            strncpy(ssid, fallback_ssid, sizeof(ssid) - 1);
            strncpy(password, fallback_pass, sizeof(password) - 1);
            ESP_LOGI(TAG_WIFI, "Using fallback WiFi credentials");
        } else {
            ESP_LOGW(TAG_WIFI, "No WiFi credentials. Entering config mode.");
            ESP_LOGW(TAG_WIFI, "Please enter: wifi <ssid> <password>");
            config_mode = true;
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_start();
            
            // Wait indefinitely for config
            xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY);
            return;
        }
    }

    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_config.sta.password, password, strlen(password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "✅ Connected to AP SSID:%s", ssid);
        
        // Save to NVS if not already there
        if (!use_nvs && fallback_ssid && fallback_pass) {
            save_wifi_credentials(ssid, password);
        }
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG_WIFI, "❌ Failed to connect to SSID:%s", ssid);
        ESP_LOGW(TAG_WIFI, "Entering config mode. Please enter: wifi <ssid> <password>");
        
        // Wait for serial config
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    } else {
        ESP_LOGE(TAG_WIFI, "❌ Unexpected event");
    }
}
