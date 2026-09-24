#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "wind_display_config.h"

esp_err_t config_manager_init(void);
bool config_manager_set_timezone_transient(const char *timezone);
const char *config_manager_get_timezone(void);
bool config_manager_set_wind_display_config(const wind_display_config_t *config);
bool config_manager_set_wind_display_config_transient(const wind_display_config_t *config);
wind_display_config_t config_manager_get_wind_display_config(void);
void config_manager_set_debug_log_enabled(bool enabled);
bool config_manager_get_debug_log_enabled(void);
