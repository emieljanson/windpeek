#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INSTALLED_CONFIGURATION_VERSION 4u
#ifdef CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003
#define WINDSCOUT_BOARD_ID "seeedstudio_reterminal_e1003"
#else
#define WINDSCOUT_BOARD_ID "seeedstudio_reterminal_e1002"
#endif
#define INSTALLED_CONFIGURATION_WRITE_BOUNDARY_COUNT 3

typedef struct {
    char id[65];
    char display_name[65];
    double latitude;
    double longitude;
    char timezone[64];
} installed_spot_t;

typedef struct {
    bool show_threshold;
    uint8_t threshold_kt;
    bool show_weather;
    bool show_temperature;
    bool show_tide;
    bool show_dedicated_footer;
    bool use_24_hour;
    bool temperature_fahrenheit;
} installed_display_configuration_t;

typedef struct {
    uint32_t version;
    uint32_t generation;
    char board_id[40];
    char device_timezone[64];
    installed_spot_t spot;
    char forecast_model[32];
    installed_display_configuration_t display;
} installed_configuration_t;

void installed_configuration_default(installed_configuration_t *config);
bool installed_configuration_validate(const installed_configuration_t *config);
uint64_t installed_configuration_digest(const installed_configuration_t *config);
esp_err_t installed_configuration_load(installed_configuration_t *out_config);
esp_err_t installed_configuration_promote(const installed_configuration_t *candidate);
esp_err_t installed_configuration_promote_setup(const installed_configuration_t *candidate,
                                                 const char *ssid, const char *password);
esp_err_t installed_configuration_load_credentials(char *ssid, size_t ssid_size,
                                                    char *password, size_t password_size);

#ifndef ESP_PLATFORM
void installed_configuration_reset_host_storage(void);
void installed_configuration_set_host_failure_boundary(int boundary);
void installed_configuration_seed_v2_host_storage(const installed_configuration_t *config,
                                                   const char *ssid, const char *password);
void installed_configuration_seed_v3_host_storage(const installed_configuration_t *config,
                                                   const char *ssid, const char *password);
#endif

#ifdef __cplusplus
}
#endif
