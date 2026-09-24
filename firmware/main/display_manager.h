#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t display_manager_init(void);
esp_err_t display_manager_begin_rgb_stream(void);
esp_err_t display_manager_push_palette_row(int y, const uint8_t *palette_row, int width);
esp_err_t display_manager_end_rgb_stream(bool show);
