#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wind_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_DISPLAY_CONFIG_VERSION 4u

typedef struct {
    uint32_t version;
    uint8_t display_mode;
    uint8_t threshold_kt;
    bool show_weather;
    bool show_temperature;
    bool show_tide;
    bool show_dedicated_footer;
    bool use_24_hour;
    bool temperature_fahrenheit;
    uint8_t wind_size;
    uint8_t swell_size;
    uint8_t module_order[5];
} wind_display_config_t;

void wind_display_config_default(wind_display_config_t *config);
bool wind_display_config_stored_version_supported(uint32_t stored_version);
bool wind_display_config_validate(const wind_display_config_t *config);
uint64_t wind_display_config_signature(const wind_display_config_t *config);

#ifdef __cplusplus
}
#endif
