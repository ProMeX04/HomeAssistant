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
#define WIFI_SSID      "Nguyen Van Hai"
#define WIFI_PASS      "0964822864"
#define MQTT_BROKER    "mqtt://laihieu2714.ddns.net"

// --- PIN DEFINITIONS ---
#define PIN_SERVO      18
#define PIN_LIGHT1     19
#define PIN_LIGHT2     21
#define PIN_FAN1       22
#define PIN_FAN2       23
#define PIN_BUZZER     25
#define PIN_DHT11      26

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

static esp_mqtt_client_handle_t client;

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
    io_conf.pin_bit_mask = (1ULL << PIN_LIGHT1) | (1ULL << PIN_LIGHT2) |
                           (1ULL << PIN_FAN1) | (1ULL << PIN_FAN2) |
                           (1ULL << PIN_BUZZER);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // Turn all off initially
    gpio_set_level(PIN_LIGHT1, 0);
    gpio_set_level(PIN_LIGHT2, 0);
    gpio_set_level(PIN_FAN1, 0);
    gpio_set_level(PIN_FAN2, 0);
    gpio_set_level(PIN_BUZZER, 0);

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
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
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

                if (strcmp(dev_name, "servo") == 0) {
                    // Limit servo range 120-180 as requested
                    if (val < 120) val = 120;
                    if (val > 180) val = 180;
                    set_servo_angle(val);
                } 
                else if (strcmp(dev_name, "light1") == 0) gpio_set_level(PIN_LIGHT1, val ? 1 : 0);
                else if (strcmp(dev_name, "light2") == 0) gpio_set_level(PIN_LIGHT2, val ? 1 : 0);
                else if (strcmp(dev_name, "fan1") == 0) gpio_set_level(PIN_FAN1, val ? 1 : 0);
                else if (strcmp(dev_name, "fan2") == 0) gpio_set_level(PIN_FAN2, val ? 1 : 0);
                else if (strcmp(dev_name, "buzzer") == 0) gpio_set_level(PIN_BUZZER, val ? 1 : 0);
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
    wifi_init_sta();
    mqtt_app_start();
}
