#ifndef _MP3_PLAYER_MODE_H_
#define _MP3_PLAYER_MODE_H_

#include "esp_err.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MP3 player mode
 * @return ESP_OK on success
 */
esp_err_t mp3_mode_init(void);

/**
 * @brief Deinitialize MP3 player mode
 */
void mp3_mode_deinit(void);

/**
 * @brief Start MP3 player mode
 */
void mp3_mode_start(void);

/**
 * @brief Stop MP3 player mode
 */
void mp3_mode_stop(void);

/**
 * @brief Play track by index
 * @param index Track index
 */
void mp3_play_track(int index);

/**
 * @brief Play next track
 */
void mp3_next_track(void);

/**
 * @brief Play previous track
 */
void mp3_prev_track(void);

/**
 * @brief Pause playback
 */
void mp3_pause(void);

/**
 * @brief Resume playback
 */
void mp3_resume(void);

/**
 * @brief Stop playback
 */
void mp3_stop(void);

/**
 * @brief Get playlist count
 * @return Number of MP3 files
 */
int mp3_get_playlist_count(void);

/**
 * @brief Check if playing
 * @return true if playing
 */
bool mp3_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // _MP3_PLAYER_MODE_H_
