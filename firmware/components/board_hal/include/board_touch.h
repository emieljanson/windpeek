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
/* Enable coordinate input after a physical button press. */
esp_err_t board_hal_touch_enable(void);
/* Hold the controller in reset, including through deep sleep. */
esp_err_t board_hal_touch_prepare_sleep(void);

#ifdef __cplusplus
}
#endif
