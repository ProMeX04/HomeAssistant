#include "settings.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SETTINGS";
static const char *NVS_NAMESPACE = "app_settings";

// Current settings in memory
static app_settings_t g_settings = {
    .volume = DEFAULT_VOLUME,
    .mic_gain = DEFAULT_MIC_GAIN,
    .auto_wake = DEFAULT_AUTO_WAKE
};

// Mutex for thread-safe access
static SemaphoreHandle_t settings_mutex = NULL;

// Helper function to clamp values
static int clamp(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

esp_err_t settings_init(void) {
    ESP_LOGI(TAG, "Initializing settings module");
    
    // Create mutex
    settings_mutex = xSemaphoreCreateMutex();
    if (settings_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create settings mutex");
        return ESP_FAIL;
    }
    
    // Load settings from NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    if (err == ESP_OK) {
        // Load volume
        int32_t volume;
        err = nvs_get_i32(nvs_handle, "volume", &volume);
        if (err == ESP_OK) {
            g_settings.volume = clamp(volume, 0, 100);
            ESP_LOGI(TAG, "Loaded volume: %d", g_settings.volume);
        } else {
            ESP_LOGW(TAG, "Volume not found in NVS, using default: %d", DEFAULT_VOLUME);
        }
        
        // Load mic gain
        int32_t mic_gain;
        err = nvs_get_i32(nvs_handle, "mic_gain", &mic_gain);
        if (err == ESP_OK) {
            g_settings.mic_gain = clamp(mic_gain, -10, 10);
            ESP_LOGI(TAG, "Loaded mic gain: %d", g_settings.mic_gain);
        } else {
            ESP_LOGW(TAG, "Mic gain not found in NVS, using default: %d", DEFAULT_MIC_GAIN);
        }
        
        // Load auto wake
        uint8_t auto_wake;
        err = nvs_get_u8(nvs_handle, "auto_wake", &auto_wake);
        if (err == ESP_OK) {
            g_settings.auto_wake = (auto_wake != 0);
            ESP_LOGI(TAG, "Loaded auto wake: %s", g_settings.auto_wake ? "enabled" : "disabled");
        } else {
            ESP_LOGW(TAG, "Auto wake not found in NVS, using default: %s", 
                     DEFAULT_AUTO_WAKE ? "enabled" : "disabled");
        }
        
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Settings loaded from NVS");
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Settings namespace not found, using defaults");
        // Save defaults to NVS
        settings_save(&g_settings);
    } else {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

esp_err_t settings_get(app_settings_t *settings) {
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(settings, &g_settings, sizeof(app_settings_t));
        xSemaphoreGive(settings_mutex);
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

int settings_get_volume(void) {
    int volume = DEFAULT_VOLUME;
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) == pdTRUE) {
        volume = g_settings.volume;
        xSemaphoreGive(settings_mutex);
    }
    return volume;
}

int settings_get_mic_gain(void) {
    int gain = DEFAULT_MIC_GAIN;
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) == pdTRUE) {
        gain = g_settings.mic_gain;
        xSemaphoreGive(settings_mutex);
    }
    return gain;
}

bool settings_get_auto_wake(void) {
    bool enabled = DEFAULT_AUTO_WAKE;
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) == pdTRUE) {
        enabled = g_settings.auto_wake;
        xSemaphoreGive(settings_mutex);
    }
    return enabled;
}

esp_err_t settings_set_volume(int volume) {
    volume = clamp(volume, 0, 100);
    
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    g_settings.volume = volume;
    xSemaphoreGive(settings_mutex);
    
    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs_handle, "volume", volume);
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Volume saved to NVS: %d", volume);
            } else {
                ESP_LOGE(TAG, "Failed to commit volume to NVS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "Failed to set volume in NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for volume: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t settings_set_mic_gain(int gain) {
    gain = clamp(gain, -10, 10);
    
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    g_settings.mic_gain = gain;
    xSemaphoreGive(settings_mutex);
    
    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs_handle, "mic_gain", gain);
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Mic gain saved to NVS: %d", gain);
            } else {
                ESP_LOGE(TAG, "Failed to commit mic gain to NVS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "Failed to set mic gain in NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for mic gain: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t settings_set_auto_wake(bool enabled) {
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    g_settings.auto_wake = enabled;
    xSemaphoreGive(settings_mutex);
    
    // Save to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs_handle, "auto_wake", enabled ? 1 : 0);
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Auto wake saved to NVS: %s", enabled ? "enabled" : "disabled");
            } else {
                ESP_LOGE(TAG, "Failed to commit auto wake to NVS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "Failed to set auto wake in NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for auto wake: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t settings_save(app_settings_t *settings) {
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate and clamp values
    settings->volume = clamp(settings->volume, 0, 100);
    settings->mic_gain = clamp(settings->mic_gain, -10, 10);
    
    if (xSemaphoreTake(settings_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    memcpy(&g_settings, settings, sizeof(app_settings_t));
    xSemaphoreGive(settings_mutex);
    
    // Save all to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for saving: %s", esp_err_to_name(err));
        return err;
    }
    
    // Save volume
    err = nvs_set_i32(nvs_handle, "volume", settings->volume);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save volume: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Save mic gain
    err = nvs_set_i32(nvs_handle, "mic_gain", settings->mic_gain);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save mic gain: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Save auto wake
    err = nvs_set_u8(nvs_handle, "auto_wake", settings->auto_wake ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save auto wake: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "All settings saved to NVS (volume=%d, mic_gain=%d, auto_wake=%s)",
                 settings->volume, settings->mic_gain, settings->auto_wake ? "on" : "off");
    }
    
    nvs_close(nvs_handle);
    return err;
}

esp_err_t settings_reset(void) {
    ESP_LOGI(TAG, "Resetting settings to defaults");
    
    app_settings_t defaults = {
        .volume = DEFAULT_VOLUME,
        .mic_gain = DEFAULT_MIC_GAIN,
        .auto_wake = DEFAULT_AUTO_WAKE
    };
    
    return settings_save(&defaults);
}
