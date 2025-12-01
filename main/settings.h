#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include "esp_err.h"
#include <stdbool.h>

// Default Settings
#define DEFAULT_VOLUME              80
#define DEFAULT_MIC_GAIN            0
#define DEFAULT_AUTO_WAKE           true

// Settings structure
typedef struct {
    int volume;          // Audio volume (0-100)
    int mic_gain;        // Microphone gain (-10 to 10)
    bool auto_wake;      // Auto wake word detection
} app_settings_t;

/**
 * @brief Initialize settings module and load settings from NVS
 * 
 * @return ESP_OK on success
 */
esp_err_t settings_init(void);

/**
 * @brief Get current settings
 * 
 * @param settings Pointer to settings structure to fill
 * @return ESP_OK on success
 */
esp_err_t settings_get(app_settings_t *settings);

/**
 * @brief Get specific setting - Volume
 * 
 * @return Current volume (0-100)
 */
int settings_get_volume(void);

/**
 * @brief Get specific setting - Microphone Gain
 * 
 * @return Current mic gain (-10 to 10)
 */
int settings_get_mic_gain(void);

/**
 * @brief Get specific setting - Auto Wake
 * 
 * @return true if auto wake is enabled
 */
bool settings_get_auto_wake(void);

/**
 * @brief Set specific setting - Volume
 * 
 * @param volume Volume to set (0-100)
 * @return ESP_OK on success
 */
esp_err_t settings_set_volume(int volume);

/**
 * @brief Set specific setting - Microphone Gain
 * 
 * @param gain Microphone gain to set (-10 to 10)
 * @return ESP_OK on success
 */
esp_err_t settings_set_mic_gain(int gain);

/**
 * @brief Set specific setting - Auto Wake
 * 
 * @param enabled Enable/disable auto wake
 * @return ESP_OK on success
 */
esp_err_t settings_set_auto_wake(bool enabled);

/**
 * @brief Save all settings to NVS
 * 
 * @param settings Settings to save
 * @return ESP_OK on success
 */
esp_err_t settings_save(app_settings_t *settings);

/**
 * @brief Reset all settings to defaults
 * 
 * @return ESP_OK on success
 */
esp_err_t settings_reset(void);

#endif // _SETTINGS_H_
