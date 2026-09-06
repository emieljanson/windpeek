#include "wind_spots.h"

#include "installed_configuration.h"

static installed_configuration_t s_configuration;
static wind_spot_t s_spot;
static bool s_loaded;

esp_err_t wind_spots_use_configuration(const installed_configuration_t *configuration)
{
    if (!installed_configuration_validate(configuration)) return ESP_ERR_INVALID_ARG;
    s_configuration = *configuration;
    s_spot = (wind_spot_t) {
        .id = s_configuration.spot.id,
        .display_name = s_configuration.spot.display_name,
        .latitude = s_configuration.spot.latitude,
        .longitude = s_configuration.spot.longitude,
        .timezone = s_configuration.spot.timezone,
    };
    s_loaded = true;
    return ESP_OK;
}

esp_err_t wind_spots_reload_installed(void)
{
    esp_err_t result = installed_configuration_load(&s_configuration);
    if (result != ESP_OK) return result;
    return wind_spots_use_configuration(&s_configuration);
}

static void ensure_loaded(void)
{
    if (!s_loaded) (void) wind_spots_reload_installed();
}

const char *wind_spots_device_timezone(void)
{
    ensure_loaded();
    return s_configuration.device_timezone;
}

size_t wind_spots_count(void)
{
    ensure_loaded();
    return 1;
}

const wind_spot_t *wind_spots_at(size_t index)
{
    ensure_loaded();
    return index == 0 ? &s_spot : NULL;
}

size_t wind_spots_offset(size_t current, int direction)
{
    (void) current;
    (void) direction;
    return 0;
}

esp_err_t wind_spots_load_selected(size_t *out_index)
{
    if (!out_index) return ESP_ERR_INVALID_ARG;
    *out_index = 0;
    return ESP_OK;
}

esp_err_t wind_spots_store_selected(size_t index)
{
    return index == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}
