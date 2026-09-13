#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct { unsigned contacts; uint16_t x, y; } board_touch_sample_t;
#ifdef __cplusplus
extern "C" {
#endif

/* E1003 only. ESP_ERR_NOT_FINISHED means no new controller frame. */
esp_err_t board_hal_touch_read(board_touch_sample_t *sample);
bool board_hal_touch_available(void);
int board_hal_touch_wake_level(void);
/* Prepare double-tap wake. On failure, retain normal touch wake if available. */
esp_err_t board_hal_touch_prepare_sleep(void);
/* Latched before resetting a controller that was in gesture mode at boot. */
bool board_hal_touch_gesture_wake(void);
bool board_hal_touch_double_tap(void);
/* Consume the last double-tap position once; false means wake without action. */
bool board_hal_touch_wake_sample(board_touch_sample_t *sample);

#ifdef __cplusplus
}
#endif
