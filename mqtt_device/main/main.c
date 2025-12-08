#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "cJSON.h"
#include "rom/ets_sys.h"

static const char *TAG = "MQTT_DEVICE";

// --- CONFIGURATION ---
#define MQTT_BROKER    "mqtt://laihieu2714.ddns.net"

// --- NVS KEYS ---
#define NVS_NAMESPACE  "wifi_config"
#define NVS_SSID_KEY   "ssid"
#define NVS_PASS_KEY   "password"

#define NVS_DEVICE_NAMESPACE "device_state"
#define NVS_LIGHT1_KEY      "light1"
#define NVS_LIGHT2_KEY      "light2"
#define NVS_LIGHT3_KEY      "light3"
#define NVS_FAN1_KEY        "fan1"
#define NVS_FAN2_KEY        "fan2"
#define NVS_SERVO_KEY       "servo"
#define NVS_BUZZER_MODE_KEY "buzzer_mode"

// --- PIN DEFINITIONS ---
#define PIN_SERVO      18
#define PIN_LIGHT1     19
#define PIN_LIGHT2     21
#define PIN_LIGHT3     17
#define PIN_FAN1       22  // Fan 1
#define PIN_FAN2       23
#define PIN_BUZZER     25
#define PIN_DHT11      26

// --- WIFI STATUS LED ---
#define LED_WIFI_STATUS 2  // GPIO 2 for WiFi status indicator

// --- LIGHT MODES ---
#define LIGHT_OFF      0  // Tắt
#define LIGHT_ON       1  // Bật
#define LIGHT_BLINKING 2  // Nhấp nháy

// --- BUZZER MODES ---
#define BUZZER_OFF        0  // Tắt
#define BUZZER_ALARM      1  // Nháy (báo động)
#define BUZZER_CONTINUOUS 2  // Liên tục

// --- SERVO CONFIG ---
#define SERVO_MIN_PULSEWIDTH_US 500  // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US 2500 // Maximum pulse width in microsecond
#define SERVO_MIN_DEGREE        0    // Minimum angle
#define SERVO_MAX_DEGREE        180  // Maximum angle
#define SERVO_TIMER             LEDC_TIMER_0
#define SERVO_MODE              LEDC_LOW_SPEED_MODE
#define SERVO_CHANNEL           LEDC_CHANNEL_0
#define SERVO_DUTY_RES          LEDC_TIMER_13_BIT // Resolution of PWM duty
#define SERVO_FREQUENCY         50                // Frequency in Hertz. Set frequency at 50Hz

// --- GLOBAL VARIABLES ---
static esp_mqtt_client_handle_t client;
static bool wifi_connected = false;
static bool config_mode = false;

// --- DEVICE STATE STRUCTURE ---
typedef struct {
    uint8_t light1;       // 0=off, 1=on, 2=blinking
    uint8_t light2;       // 0=off, 1=on, 2=blinking
    uint8_t light3;       // 0=off, 1=on, 2=blinking
    uint8_t fan1;
    uint8_t fan2;
    int32_t servo_angle;
    int32_t buzzer_mode;
} device_state_t;

static device_state_t device_state = {LIGHT_OFF, LIGHT_OFF, LIGHT_OFF, 0, 0, 120, BUZZER_OFF};

// --- BUZZER STATE ---
static int buzzer_timeout_sec = 0;  // 0 = no timeout
static TaskHandle_t buzzer_task_handle = NULL;

// --- LIGHT BLINKING TASK ---
static TaskHandle_t light_blink_task_handle = NULL;

// --- FORWARD DECLARATIONS ---
static void mqtt_app_start(void);
static void sensor_task(void *pvParameters);
static void buzzer_task(void *pvParameters);
static void light_blink_task(void *pvParameters);

// --- DHT11 READER (Simple Bit-Banging) ---
typedef struct {
    int temperature;
    int humidity;
} dht11_reading_t;

static int wait_for_state(int state, int timeout_us) {
    int count = 0;
    while (gpio_get_level(PIN_DHT11) == state) {
        if (count++ > timeout_us) return -1;
        ets_delay_us(1);
    }
    return count;
}

dht11_reading_t read_dht11() {
    dht11_reading_t reading = {0, 0};
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // Start signal
    gpio_set_direction(PIN_DHT11, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_DHT11, 0);
    vTaskDelay(pdMS_TO_TICKS(20)); // Pull low for 20ms
    gpio_set_level(PIN_DHT11, 1);
    ets_delay_us(40);
    gpio_set_direction(PIN_DHT11, GPIO_MODE_INPUT);

    // Response
    if (wait_for_state(0, 80) == -1) return reading;
    if (wait_for_state(1, 80) == -1) return reading;

    // Read 40 bits
    for (int i = 0; i < 40; i++) {
        if (wait_for_state(0, 50) == -1) return reading;
        int duration = wait_for_state(1, 70);
        if (duration == -1) return reading;
        
        if (duration > 28) {
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    // Verify Checksum
    if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        reading.humidity = data[0];
        reading.temperature = data[2];
    } else {
        ESP_LOGW(TAG, "DHT11 Checksum Error");
    }
    
    return reading;
}

// --- HARDWARE INIT ---
void init_hardware() {
    // GPIO Outputs
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_LIGHT1) | (1ULL << PIN_LIGHT2) | (1ULL << PIN_LIGHT3) |
                           (1ULL << PIN_FAN1) | (1ULL << PIN_FAN2) |
                           (1ULL << PIN_BUZZER) | (1ULL << LED_WIFI_STATUS);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // Turn all off initially
    gpio_set_level(PIN_LIGHT1, 0);
    gpio_set_level(PIN_LIGHT2, 0);
    gpio_set_level(PIN_LIGHT3, 0);
    gpio_set_level(PIN_FAN1, 0);
    gpio_set_level(PIN_FAN2, 0);
    gpio_set_level(PIN_BUZZER, 0);
    gpio_set_level(LED_WIFI_STATUS, 0);  // LED off initially

    // Servo PWM Init
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = SERVO_DUTY_RES,
        .freq_hz = SERVO_FREQUENCY,
        .speed_mode = SERVO_MODE,
        .timer_num = SERVO_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel = SERVO_CHANNEL,
        .duty = 0,
        .gpio_num = PIN_SERVO,
        .speed_mode = SERVO_MODE,
        .hpoint = 0,
        .timer_sel = SERVO_TIMER,
    };
    ledc_channel_config(&ledc_channel);
}

// --- SERVO CONTROL ---
void set_servo_angle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    uint32_t duty = (angle * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / SERVO_MAX_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
    // Convert to duty cycle (resolution 13 bit = 8192)
    // Period = 20000us (50Hz)
    uint32_t duty_cycle = (duty * 8192) / 20000;

    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty_cycle);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
    ESP_LOGI(TAG, "Servo set to %d degrees", angle);
}

// --- WIFI HELPER ---
static int wifi_retry_count = 0;
#define MAX_WIFI_RETRY 10

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi started, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_retry_count++;
        if (wifi_retry_count < MAX_WIFI_RETRY) {
            esp_wifi_connect();
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", wifi_retry_count, MAX_WIFI_RETRY);
        } else {
            ESP_LOGW(TAG, "Failed to connect to WiFi after %d attempts", MAX_WIFI_RETRY);
            ESP_LOGW(TAG, "Entering config mode. Please enter: wifi <ssid> <password>");
            config_mode = true;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        wifi_retry_count = 0;
        config_mode = false;
        
        // Bật LED báo WiFi connected
        gpio_set_level(LED_WIFI_STATUS, 1);
        ESP_LOGI(TAG, "WiFi Status LED ON (GPIO 2)");
    }
}

// Load WiFi credentials from NVS
static bool load_wifi_credentials(char *ssid, char *password, size_t max_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi credentials found in NVS");
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
    ESP_LOGI(TAG, "Loaded WiFi credentials from NVS: %s", ssid);
    return true;
}

// Save WiFi credentials to NVS
static bool save_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
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
        ESP_LOGI(TAG, "WiFi credentials saved to NVS");
        return true;
    }
    return false;
}

// --- DEVICE STATE NVS FUNCTIONS ---
// Save device state to NVS
static bool save_device_state(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_DEVICE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for device state");
        return false;
    }

    // Save each device state
    nvs_set_u8(nvs_handle, NVS_LIGHT1_KEY, device_state.light1);
    nvs_set_u8(nvs_handle, NVS_LIGHT2_KEY, device_state.light2);
    nvs_set_u8(nvs_handle, NVS_LIGHT3_KEY, device_state.light3);
    nvs_set_u8(nvs_handle, NVS_FAN1_KEY, device_state.fan1);
    nvs_set_u8(nvs_handle, NVS_FAN2_KEY, device_state.fan2);
    nvs_set_i32(nvs_handle, NVS_SERVO_KEY, device_state.servo_angle);
    nvs_set_i32(nvs_handle, NVS_BUZZER_MODE_KEY, device_state.buzzer_mode);

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Device state saved to NVS");
        return true;
    }
    return false;
}

// Load device state from NVS
static bool load_device_state(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_DEVICE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved device state found in NVS, using defaults");
        return false;
    }

    // Load each device state (use current value as default if not found)
    nvs_get_u8(nvs_handle, NVS_LIGHT1_KEY, &device_state.light1);
    nvs_get_u8(nvs_handle, NVS_LIGHT2_KEY, &device_state.light2);
    nvs_get_u8(nvs_handle, NVS_LIGHT3_KEY, &device_state.light3);
    nvs_get_u8(nvs_handle, NVS_FAN1_KEY, &device_state.fan1);
    nvs_get_u8(nvs_handle, NVS_FAN2_KEY, &device_state.fan2);
    nvs_get_i32(nvs_handle, NVS_SERVO_KEY, &device_state.servo_angle);
    nvs_get_i32(nvs_handle, NVS_BUZZER_MODE_KEY, &device_state.buzzer_mode);

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Device state loaded from NVS");
    ESP_LOGI(TAG, "  Light1=%d, Light2=%d, Light3=%d", device_state.light1, device_state.light2, device_state.light3);
    ESP_LOGI(TAG, "  Fan1=%d, Fan2=%d", device_state.fan1, device_state.fan2);
    ESP_LOGI(TAG, "  Servo=%ld, Buzzer=%ld", (long)device_state.servo_angle, (long)device_state.buzzer_mode);
    return true;
}

// Apply loaded device state to hardware
static void apply_device_state(void) {
    // Lights will be controlled by light_blink_task based on their mode
    // Only set static states here (OFF or ON), blinking is handled by task
    if (device_state.light1 == LIGHT_OFF) gpio_set_level(PIN_LIGHT1, 0);
    else if (device_state.light1 == LIGHT_ON) gpio_set_level(PIN_LIGHT1, 1);
    
    if (device_state.light2 == LIGHT_OFF) gpio_set_level(PIN_LIGHT2, 0);
    else if (device_state.light2 == LIGHT_ON) gpio_set_level(PIN_LIGHT2, 1);
    
    if (device_state.light3 == LIGHT_OFF) gpio_set_level(PIN_LIGHT3, 0);
    else if (device_state.light3 == LIGHT_ON) gpio_set_level(PIN_LIGHT3, 1);
    
    gpio_set_level(PIN_FAN1, device_state.fan1);
    gpio_set_level(PIN_FAN2, device_state.fan2);
    set_servo_angle(device_state.servo_angle);
    // Note: buzzer_mode will be applied by buzzer_task
    ESP_LOGI(TAG, "Device state applied to hardware");
}

void wifi_init_sta(void) {
    char ssid[32] = {0};
    char password[64] = {0};

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    // Try to load credentials from NVS
    if (!load_wifi_credentials(ssid, password, sizeof(ssid))) {
        ESP_LOGW(TAG, "No saved WiFi credentials. Entering config mode.");
        ESP_LOGW(TAG, "Please enter: wifi <ssid> <password>");
        config_mode = true;
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        return;
    }

    // Configure WiFi with loaded credentials
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_config.sta.password, password, strlen(password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// --- LIGHT BLINKING TASK ---
static void light_blink_task(void *pvParameters) {
    bool blink_state = false;
    
    while (1) {
        // Toggle blink state every 500ms
        blink_state = !blink_state;
        
        // Light 1
        if (device_state.light1 == LIGHT_OFF) {
            gpio_set_level(PIN_LIGHT1, 0);
        } else if (device_state.light1 == LIGHT_ON) {
            gpio_set_level(PIN_LIGHT1, 1);
        } else if (device_state.light1 == LIGHT_BLINKING) {
            gpio_set_level(PIN_LIGHT1, blink_state ? 1 : 0);
        }
        
        // Light 2
        if (device_state.light2 == LIGHT_OFF) {
            gpio_set_level(PIN_LIGHT2, 0);
        } else if (device_state.light2 == LIGHT_ON) {
            gpio_set_level(PIN_LIGHT2, 1);
        } else if (device_state.light2 == LIGHT_BLINKING) {
            gpio_set_level(PIN_LIGHT2, blink_state ? 1 : 0);
        }
        
        // Light 3
        if (device_state.light3 == LIGHT_OFF) {
            gpio_set_level(PIN_LIGHT3, 0);
        } else if (device_state.light3 == LIGHT_ON) {
            gpio_set_level(PIN_LIGHT3, 1);
        } else if (device_state.light3 == LIGHT_BLINKING) {
            gpio_set_level(PIN_LIGHT3, blink_state ? 1 : 0);
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));  // Blink every 500ms
    }
}

// --- BUZZER TASK ---
static void buzzer_task(void *pvParameters) {
    int elapsed_sec = 0;
    
    while (1) {
        if (device_state.buzzer_mode == BUZZER_OFF) {
            gpio_set_level(PIN_BUZZER, 0);
            elapsed_sec = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Check timeout
        if (buzzer_timeout_sec > 0 && elapsed_sec >= buzzer_timeout_sec) {
            ESP_LOGI(TAG, "Buzzer timeout reached (%d sec)", buzzer_timeout_sec);
            device_state.buzzer_mode = BUZZER_OFF;
            gpio_set_level(PIN_BUZZER, 0);
            elapsed_sec = 0;
            save_device_state();  // Save state after timeout
            continue;
        }
        
        if (device_state.buzzer_mode == BUZZER_ALARM) {
            // Nháy: 200ms ON, 200ms OFF
            gpio_set_level(PIN_BUZZER, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(PIN_BUZZER, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            elapsed_sec++;  // ~400ms per cycle, approximate
        } else if (device_state.buzzer_mode == BUZZER_CONTINUOUS) {
            // Liên tục
            gpio_set_level(PIN_BUZZER, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            elapsed_sec++;
        }
    }
}

// --- MQTT HANDLER ---
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        esp_mqtt_client_subscribe(client, "device/control", 0);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT Data received");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        // Parse JSON
        cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
        if (root) {
            cJSON *device = cJSON_GetObjectItem(root, "device");
            cJSON *value = cJSON_GetObjectItem(root, "value");

            if (cJSON_IsString(device) && cJSON_IsNumber(value)) {
                const char *dev_name = device->valuestring;
                int val = value->valueint;
                bool state_changed = false;

                if (strcmp(dev_name, "servo") == 0) {
                    // Limit servo range 120-180 as requested
                    if (val < 120) val = 120;
                    if (val > 180) val = 180;
                    device_state.servo_angle = val;
                    set_servo_angle(val);
                    state_changed = true;
                } 
                else if (strcmp(dev_name, "light1") == 0) {
                    // Light modes: 0=off, 1=on, 2=blinking
                    if (val < 0) val = 0;
                    if (val > 2) val = 2;
                    device_state.light1 = val;
                    state_changed = true;
                    ESP_LOGI(TAG, "Light1 set to mode %d (0=off, 1=on, 2=blink)", val);
                }
                else if (strcmp(dev_name, "light2") == 0) {
                    // Light modes: 0=off, 1=on, 2=blinking
                    if (val < 0) val = 0;
                    if (val > 2) val = 2;
                    device_state.light2 = val;
                    state_changed = true;
                    ESP_LOGI(TAG, "Light2 set to mode %d (0=off, 1=on, 2=blink)", val);
                }
                else if (strcmp(dev_name, "light3") == 0) {
                    // Light modes: 0=off, 1=on, 2=blinking
                    if (val < 0) val = 0;
                    if (val > 2) val = 2;
                    device_state.light3 = val;
                    state_changed = true;
                    ESP_LOGI(TAG, "Light3 set to mode %d (0=off, 1=on, 2=blink)", val);
                }
                else if (strcmp(dev_name, "fan1") == 0) {
                    device_state.fan1 = val ? 1 : 0;
                    gpio_set_level(PIN_FAN1, device_state.fan1);
                    state_changed = true;
                }
                else if (strcmp(dev_name, "fan2") == 0) {
                    device_state.fan2 = val ? 1 : 0;
                    gpio_set_level(PIN_FAN2, device_state.fan2);
                    state_changed = true;
                }
                else if (strcmp(dev_name, "buzzer") == 0) {
                    // Buzzer modes: 0=off, 1=alarm (nháy), 2=continuous (liên tục)
                    // Optional "timeout" field for auto-off (in seconds)
                    cJSON *timeout = cJSON_GetObjectItem(root, "timeout");
                    
                    device_state.buzzer_mode = val;
                    buzzer_timeout_sec = (timeout && cJSON_IsNumber(timeout)) ? timeout->valueint : 0;
                    state_changed = true;
                    
                    if (device_state.buzzer_mode == BUZZER_OFF) {
                        ESP_LOGI(TAG, "Buzzer OFF");
                    } else if (device_state.buzzer_mode == BUZZER_ALARM) {
                        ESP_LOGI(TAG, "Buzzer ALARM mode (timeout: %d sec)", buzzer_timeout_sec);
                    } else if (device_state.buzzer_mode == BUZZER_CONTINUOUS) {
                        ESP_LOGI(TAG, "Buzzer CONTINUOUS mode (timeout: %d sec)", buzzer_timeout_sec);
                    }
                }
                
                // Save state to NVS after any device change
                if (state_changed) {
                    save_device_state();
                }
                else if (strcmp(dev_name, "sensor") == 0) {
                    // Read DHT11 on demand
                    dht11_reading_t reading = read_dht11();
                    if (reading.temperature > 0 && reading.humidity > 0) {
                        char payload[100];
                        ESP_LOGI(TAG, "Temp: %d C, Hum: %d %%", reading.temperature, reading.humidity);
                        snprintf(payload, sizeof(payload), "{\"temp\": %d, \"hum\": %d}", reading.temperature, reading.humidity);
                        esp_mqtt_client_publish(client, "device/status", payload, 0, 0, 0);
                    } else {
                        ESP_LOGW(TAG, "Failed to read DHT11");
                    }
                }
            }
            cJSON_Delete(root);
        }
        break;
    default:
        break;
    }
}

// --- SERIAL CONFIG TASK ---
static void serial_config_task(void *pvParameters) {
    char line[128];
    int pos = 0;
    
    ESP_LOGI(TAG, "Serial config task started");
    
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
                                ESP_LOGW(TAG, "SSID too long or empty");
                                pos = 0;
                                continue;
                            }
                        } else {
                            ESP_LOGW(TAG, "Missing closing quote for SSID");
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
                            ESP_LOGW(TAG, "SSID too long or empty");
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
                                ESP_LOGW(TAG, "Password too long or empty");
                                pos = 0;
                                continue;
                            }
                        } else {
                            ESP_LOGW(TAG, "Missing closing quote for password");
                            pos = 0;
                            continue;
                        }
                    } else {
                        // Unquoted password - take rest of line
                        if (strlen(input) > 0 && strlen(input) < sizeof(password)) {
                            strncpy(password, input, sizeof(password) - 1);
                        } else if (strlen(input) == 0) {
                            ESP_LOGW(TAG, "Password is empty");
                            pos = 0;
                            continue;
                        } else {
                            ESP_LOGW(TAG, "Password too long");
                            pos = 0;
                            continue;
                        }
                    }
                    
                    // Validate and save
                    if (strlen(ssid) > 0 && strlen(password) > 0) {
                        ESP_LOGI(TAG, "Received WiFi config - SSID: %s", ssid);
                            
                            // Save to NVS
                            if (save_wifi_credentials(ssid, password)) {
                                ESP_LOGI(TAG, "WiFi credentials saved. Restarting WiFi...");
                                
                                // Stop WiFi
                                esp_wifi_stop();
                                vTaskDelay(pdMS_TO_TICKS(500));
                                
                                // Reconfigure and start
                                wifi_config_t wifi_config = {0};
                                memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
                                memcpy(wifi_config.sta.password, password, strlen(password));
                                wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                                
                                esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
                                wifi_retry_count = 0;
                                esp_wifi_start();
                                
                                ESP_LOGI(TAG, "Connecting to WiFi...");
                                
                                // Wait for connection
                                int retry = 0;
                                while (!wifi_connected && retry < 30) {
                                    vTaskDelay(pdMS_TO_TICKS(1000));
                                    retry++;
                                }
                                
                                if (wifi_connected && client == NULL) {
                                    ESP_LOGI(TAG, "WiFi connected! Starting MQTT and sensors...");
                                    mqtt_app_start();
                                    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
                                }
                            } else {
                                ESP_LOGE(TAG, "Failed to save WiFi credentials");
                            }
                    } else {
                        ESP_LOGW(TAG, "Invalid credentials. SSID or password is empty");
                    }
                } else {
                    ESP_LOGW(TAG, "Unknown command. Use: wifi \"ssid\" password or wifi ssid password");
                }
                
                pos = 0;
            }
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = c;
        }
    }
}

// --- SENSOR READING TASK ---
static void sensor_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10000); // 10 seconds
    
    // Wait for MQTT to connect first
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    while (1) {
        if (client != NULL && wifi_connected) {
            dht11_reading_t reading = read_dht11();
            if (reading.temperature > 0 && reading.humidity > 0) {
                char payload[100];
                ESP_LOGI(TAG, "Auto-reading: Temp: %d C, Hum: %d %%", reading.temperature, reading.humidity);
                snprintf(payload, sizeof(payload), "{\"temp\": %d, \"hum\": %d}", reading.temperature, reading.humidity);
                esp_mqtt_client_publish(client, "device/sensor", payload, 0, 0, 0);
            } else {
                ESP_LOGW(TAG, "Failed to read DHT11");
            }
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

void app_main(void) {
    ESP_LOGI(TAG, "Startup...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_hardware();
    
    // Load and apply saved device state from NVS
    ESP_LOGI(TAG, "Loading device state from NVS...");
    if (load_device_state()) {
        apply_device_state();
    } else {
        ESP_LOGI(TAG, "Using default device state");
    }
    
    
    // Start light blinking task
    xTaskCreate(light_blink_task, "light_blink_task", 2048, NULL, 5, &light_blink_task_handle);
    
    // Start buzzer task
    xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 5, &buzzer_task_handle);
    
    // Start serial config task (always running, but only active in config mode)
    xTaskCreate(serial_config_task, "serial_config", 4096, NULL, 5, NULL);
    
    // Initialize WiFi
    wifi_init_sta();
    
    // Wait for WiFi connection before starting MQTT
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    int wait_count = 0;
    while (!wifi_connected && wait_count < 60) {  // Wait max 60 seconds
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_count++;
        
        if (config_mode) {
            ESP_LOGW(TAG, "In config mode. Waiting for WiFi credentials via serial...");
            // Reset wait counter when in config mode
            wait_count = 0;
        }
    }
    
    if (wifi_connected) {
        ESP_LOGI(TAG, "WiFi connected! Starting MQTT and sensors...");
        mqtt_app_start();
        
        // Start sensor reading task
        xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    } else {
        ESP_LOGW(TAG, "Failed to connect to WiFi. MQTT and sensors not started.");
        ESP_LOGW(TAG, "Please configure WiFi via serial: wifi <ssid> <password>");
    }
}

