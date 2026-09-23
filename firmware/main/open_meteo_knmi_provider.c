#include "open_meteo_knmi_provider.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "wind_timezone.h"

#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#endif

static const char *PROVIDER_NAME = "open-meteo";
static const uint8_t REQUIRED_HOURS[WIND_FORECAST_SAMPLES_PER_DAY] = {8, 11, 14, 17, 20};
static open_meteo_knmi_diagnostics_t s_diagnostics;

void open_meteo_knmi_get_diagnostics(open_meteo_knmi_diagnostics_t *out_diagnostics)
{
    if (out_diagnostics) {
        *out_diagnostics = s_diagnostics;
    }
}

const char *open_meteo_knmi_endpoint(void)
{
    return OPEN_METEO_ENDPOINT;
}

static bool supported_model(const char *model)
{
    return wind_forecast_model_api_id(model) != NULL;
}

static bool encode_query_value(const char *input, char *output, size_t output_size)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!input || !output || output_size == 0) return false;
    size_t written = 0;
    for (const unsigned char *cursor = (const unsigned char *) input; *cursor; ++cursor) {
        const bool unreserved = isalnum(*cursor) || *cursor == '-' || *cursor == '_' ||
                                *cursor == '.' || *cursor == '~';
        const size_t needed = unreserved ? 1 : 3;
        if (written + needed >= output_size) return false;
        if (unreserved) {
            output[written++] = (char) *cursor;
        } else {
            output[written++] = '%';
            output[written++] = hex[*cursor >> 4];
            output[written++] = hex[*cursor & 0x0f];
        }
    }
    output[written] = '\0';
    return written > 0;
}

bool open_meteo_knmi_config_valid(const open_meteo_knmi_config_t *config)
{
    if (!config || !config->spot_id || config->spot_id[0] == '\0' ||
        !config->spot_name || config->spot_name[0] == '\0' || !config->timezone ||
        !config->model ||
        !supported_model(config->model) || config->timezone[0] == '\0' ||
        !isfinite(config->latitude) || config->latitude < -90.0 || config->latitude > 90.0 ||
        !isfinite(config->longitude) || config->longitude < -180.0 ||
        config->longitude > 180.0) return false;
    return true;
}

static bool copy_text(char *dst, size_t size, const char *src)
{
    if (!dst || !src || strlen(src) >= size) {
        return false;
    }
    memcpy(dst, src, strlen(src) + 1);
    return true;
}

static bool parse_local_time(const char *value, wind_local_datetime_t *local,
                             char date[WIND_FORECAST_DATE_LENGTH])
{
    int y = 0, m = 0, d = 0, h = 0, minute = 0;
    char tail = 0;
    if (!value || sscanf(value, "%4d-%2d-%2dT%2d:%2d%c", &y, &m, &d, &h, &minute, &tail) != 5 ||
        m < 1 || m > 12 || d < 1 || d > 31 || h < 0 || h > 23 || minute != 0) {
        return false;
    }
    *local = (wind_local_datetime_t) {
        .year = (int16_t) y, .month = (uint8_t) m, .day = (uint8_t) d,
        .hour = (uint8_t) h, .minute = (uint8_t) minute,
    };
    return wind_timezone_format_date(local, date, WIND_FORECAST_DATE_LENGTH) == ESP_OK;
}

static cJSON *required_object(cJSON *parent, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    return cJSON_IsObject(item) ? item : NULL;
}

static cJSON *required_array(cJSON *parent, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    return cJSON_IsArray(item) ? item : NULL;
}

typedef struct {
    cJSON *speeds;
    cJSON *gusts;
    cJSON *directions;
    cJSON *cloud_cover;
    cJSON *precipitation;
    cJSON *is_day;
    cJSON *temperature;
    bool weather_aligned;
    bool temperature_aligned;
} model_arrays_t;

static bool model_field_name(char *output, size_t output_size, const char *field,
                             const char *api_model, bool suffixed)
{
    int written = suffixed ? snprintf(output, output_size, "%s_%s", field, api_model)
                           : snprintf(output, output_size, "%s", field);
    return written > 0 && (size_t) written < output_size;
}

static bool load_model_arrays(cJSON *units, cJSON *hourly, int count, const char *api_model,
                              bool suffixed, model_arrays_t *out)
{
    static const char *fields[] = {"wind_speed_10m", "wind_gusts_10m", "wind_direction_10m",
                                   "cloud_cover", "precipitation", "is_day", "temperature_2m"};
    char names[7][96];
    for (size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        if (!model_field_name(names[index], sizeof(names[index]), fields[index], api_model,
                              suffixed)) return false;
    }
    cJSON *speed_unit = cJSON_GetObjectItemCaseSensitive(units, names[0]);
    cJSON *gust_unit = cJSON_GetObjectItemCaseSensitive(units, names[1]);
    cJSON *direction_unit = cJSON_GetObjectItemCaseSensitive(units, names[2]);
    cJSON *cloud_unit = cJSON_GetObjectItemCaseSensitive(units, names[3]);
    cJSON *precipitation_unit = cJSON_GetObjectItemCaseSensitive(units, names[4]);
    cJSON *temperature_unit = cJSON_GetObjectItemCaseSensitive(units, names[6]);
    *out = (model_arrays_t) {
        .speeds = required_array(hourly, names[0]),
        .gusts = required_array(hourly, names[1]),
        .directions = required_array(hourly, names[2]),
        .cloud_cover = required_array(hourly, names[3]),
        .precipitation = required_array(hourly, names[4]),
        .is_day = required_array(hourly, names[5]),
        .temperature = required_array(hourly, names[6]),
    };
    if (!cJSON_IsString(speed_unit) || strcmp(speed_unit->valuestring, "kn") != 0 ||
        !cJSON_IsString(gust_unit) || strcmp(gust_unit->valuestring, "kn") != 0 ||
        !cJSON_IsString(direction_unit) || strcmp(direction_unit->valuestring, "°") != 0 ||
        !out->speeds || !out->gusts || !out->directions ||
        cJSON_GetArraySize(out->speeds) != count || cJSON_GetArraySize(out->gusts) != count ||
        cJSON_GetArraySize(out->directions) != count) return false;
    out->weather_aligned = out->cloud_cover && out->precipitation && out->is_day &&
        cJSON_IsString(cloud_unit) && strcmp(cloud_unit->valuestring, "%") == 0 &&
        cJSON_IsString(precipitation_unit) && strcmp(precipitation_unit->valuestring, "mm") == 0 &&
        cJSON_GetArraySize(out->cloud_cover) == count &&
        cJSON_GetArraySize(out->precipitation) == count && cJSON_GetArraySize(out->is_day) == count;
    out->temperature_aligned = out->temperature && cJSON_IsString(temperature_unit) &&
        strcmp(temperature_unit->valuestring, "°C") == 0 &&
        cJSON_GetArraySize(out->temperature) == count;
    return true;
}

esp_err_t open_meteo_knmi_parse_json(const open_meteo_knmi_config_t *config, const char *json,
                                     size_t length, int64_t retrieved_at, const char *first_date,
                                     wind_forecast_t *out_forecast)
{
    if (!open_meteo_knmi_config_valid(config) || !json || length == 0 ||
        length > OPEN_METEO_RESPONSE_LIMIT || !first_date || !out_forecast) {
        return ESP_ERR_INVALID_ARG;
    }

    wind_forecast_t parsed;
    wind_forecast_clear(&parsed);
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    cJSON *timezone = cJSON_GetObjectItemCaseSensitive(root, "timezone");
    cJSON *units = required_object(root, "hourly_units");
    cJSON *hourly = required_object(root, "hourly");
    if (!cJSON_IsString(timezone) || strcmp(timezone->valuestring, config->timezone) != 0 ||
        !units || !hourly) {
        goto cleanup;
    }

    cJSON *times = required_array(hourly, "time");
    int count = times ? cJSON_GetArraySize(times) : 0;
    const char *api_model = wind_forecast_model_api_id(config->model);
    const bool suffixed = !required_array(hourly, "wind_speed_10m");
    model_arrays_t primary;
    model_arrays_t fallback;
    bool has_fallback = suffixed && strcmp(api_model, "best_match") != 0;
    if (count <= 0 || !load_model_arrays(units, hourly, count, api_model, suffixed, &primary) ||
        (has_fallback && !load_model_arrays(units, hourly, count, "best_match", true, &fallback))) {
        goto cleanup;
    }

    if (!copy_text(parsed.spot_id, sizeof(parsed.spot_id), config->spot_id) ||
        !copy_text(parsed.spot_name, sizeof(parsed.spot_name), config->spot_name) ||
        !copy_text(parsed.timezone, sizeof(parsed.timezone), config->timezone) ||
        !copy_text(parsed.provider, sizeof(parsed.provider), PROVIDER_NAME) ||
        !copy_text(parsed.model, sizeof(parsed.model), config->model)) {
        goto cleanup;
    }
    parsed.latitude = config->latitude;
    parsed.longitude = config->longitude;
    parsed.retrieved_at = retrieved_at;

    int selected = 0;
    int primary_selected = 0;
    char previous_time[24] = {0};
    for (int i = 0; i < count; ++i) {
        cJSON *time_item = cJSON_GetArrayItem(times, i);
        if (!cJSON_IsString(time_item) ||
            (previous_time[0] && strcmp(previous_time, time_item->valuestring) >= 0)) {
            goto cleanup;
        }
        if (strlen(time_item->valuestring) >= sizeof(previous_time)) {
            goto cleanup;
        }
        strcpy(previous_time, time_item->valuestring);

        char date[WIND_FORECAST_DATE_LENGTH];
        wind_local_datetime_t local;
        if (!parse_local_time(time_item->valuestring, &local, date)) {
            goto cleanup;
        }
        if (strcmp(date, first_date) < 0) {
            continue;
        }
        int target_slot = -1;
        for (int slot = 0; slot < WIND_FORECAST_SAMPLES_PER_DAY; ++slot) {
            if (local.hour == REQUIRED_HOURS[slot]) {
                target_slot = slot;
                break;
            }
        }
        if (local.hour < 8 || local.hour > 20) continue;
        model_arrays_t *sample_arrays = &primary;
        cJSON *speed_item = cJSON_GetArrayItem(primary.speeds, i);
        cJSON *gust_item = cJSON_GetArrayItem(primary.gusts, i);
        cJSON *direction_item = cJSON_GetArrayItem(primary.directions, i);
        if (cJSON_IsNumber(speed_item) && cJSON_IsNumber(gust_item) &&
            cJSON_IsNumber(direction_item)) {
            if (target_slot >= 0) ++primary_selected;
        } else if (has_fallback) {
            sample_arrays = &fallback;
            speed_item = cJSON_GetArrayItem(fallback.speeds, i);
            gust_item = cJSON_GetArrayItem(fallback.gusts, i);
            direction_item = cJSON_GetArrayItem(fallback.directions, i);
        }
        if (!cJSON_IsNumber(speed_item) || !cJSON_IsNumber(gust_item) ||
            !cJSON_IsNumber(direction_item) || !isfinite(speed_item->valuedouble) ||
            !isfinite(gust_item->valuedouble) || !isfinite(direction_item->valuedouble)) {
            if (target_slot < 0) continue;
            goto cleanup;
        }
        int day = selected / WIND_FORECAST_SAMPLES_PER_DAY;
        int expected_slot = selected % WIND_FORECAST_SAMPLES_PER_DAY;
        if (day >= WIND_FORECAST_DAY_COUNT) continue;
        if ((target_slot >= 0 && target_slot != expected_slot) ||
            (target_slot == 0 && day == 0 && strcmp(date, first_date) != 0) ||
            (target_slot > 0 && strcmp(parsed.days[day].local_date, date) != 0)) {
            goto cleanup;
        }
        if (target_slot == 0 && !copy_text(parsed.days[day].local_date,
                                           sizeof(parsed.days[day].local_date), date)) {
            goto cleanup;
        }
        int wind = wind_forecast_round_knots(speed_item->valuedouble);
        int gust = wind_forecast_round_knots(gust_item->valuedouble);
        uint16_t direction = wind_forecast_destination_degrees(direction_item->valuedouble);
        if (wind < 0 || gust < 0 || direction == UINT16_MAX) {
            if (target_slot < 0) continue;
            goto cleanup;
        }
        int64_t timestamp = 0;
        if (wind_timezone_to_unix(config->timezone, &local, &timestamp) != ESP_OK) goto cleanup;
        wind_forecast_sample_t normalized = {.timestamp = timestamp,
                                             .local_hour = local.hour,
                                             .wind_knots = (int16_t) wind,
                                             .gust_knots = (int16_t) gust,
                                             .destination_degrees = direction};
        if (sample_arrays->weather_aligned) {
            cJSON *cloud_item = cJSON_GetArrayItem(sample_arrays->cloud_cover, i);
            cJSON *precipitation_item = cJSON_GetArrayItem(sample_arrays->precipitation, i);
            cJSON *day_item = cJSON_GetArrayItem(sample_arrays->is_day, i);
            if (cJSON_IsNumber(cloud_item) && cJSON_IsNumber(precipitation_item) &&
                cJSON_IsNumber(day_item) && isfinite(cloud_item->valuedouble) &&
                isfinite(precipitation_item->valuedouble) &&
                isfinite(day_item->valuedouble) && cloud_item->valuedouble >= 0.0 &&
                cloud_item->valuedouble <= 100.0 && precipitation_item->valuedouble >= 0.0 &&
                precipitation_item->valuedouble <= 655.35 &&
                (day_item->valuedouble == 0.0 || day_item->valuedouble == 1.0)) {
                normalized.cloud_cover_percent = (uint8_t) lround(cloud_item->valuedouble);
                normalized.precipitation_hundredths_mm =
                    (uint16_t) lround(precipitation_item->valuedouble * 100.0);
                normalized.is_day = (uint8_t) day_item->valuedouble;
                normalized.weather_available = 1;
            }
        }
        if (sample_arrays->temperature_aligned) {
            cJSON *temperature_item = cJSON_GetArrayItem(sample_arrays->temperature, i);
            if (cJSON_IsNumber(temperature_item) && isfinite(temperature_item->valuedouble) &&
                temperature_item->valuedouble >= -3276.8 &&
                temperature_item->valuedouble <= 3276.7) {
                normalized.temperature_tenths_c =
                    (int16_t) lround(temperature_item->valuedouble * 10.0);
                normalized.temperature_available = 1;
            }
        }
        parsed.hourly[day][local.hour - 8] = normalized;
        if (target_slot >= 0) {
            parsed.days[day].samples[target_slot] = normalized;
            ++selected;
        }
    }

    if (selected != WIND_FORECAST_SAMPLE_COUNT || (has_fallback && primary_selected == 0) ||
        !wind_forecast_validate(&parsed)) {
        goto cleanup;
    }
    *out_forecast = parsed;
    result = ESP_OK;

cleanup:
    cJSON_Delete(root);
    return result;
}

#ifdef ESP_PLATFORM
typedef struct {
    char *body;
    size_t length;
    bool too_large;
} response_context_t;

static esp_err_t response_event(esp_http_client_event_t *event)
{
    response_context_t *response = (response_context_t *) event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    if (response->length + (size_t) event->data_len > OPEN_METEO_RESPONSE_LIMIT) {
        response->too_large = true;
        return ESP_FAIL;
    }
    memcpy(response->body + response->length, event->data, (size_t) event->data_len);
    response->length += (size_t) event->data_len;
    response->body[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t fetch_forecast(void *context, int64_t retrieved_at, wind_forecast_t *out_forecast)
{
    memset(&s_diagnostics, 0, sizeof(s_diagnostics));
    s_diagnostics.perform_result = ESP_ERR_NOT_FINISHED;
    s_diagnostics.parse_result = ESP_ERR_NOT_FINISHED;
    open_meteo_knmi_config_t *config = (open_meteo_knmi_config_t *) context;
    if (!open_meteo_knmi_config_valid(config) || !out_forecast) {
        return ESP_ERR_INVALID_STATE;
    }
    char encoded_timezone[192];
    if (!encode_query_value(config->timezone, encoded_timezone, sizeof(encoded_timezone))) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[768];
    const char *api_model = wind_forecast_model_api_id(config->model);
    const bool needs_fallback = strcmp(api_model, "best_match") != 0;
    int written = snprintf(
        url, sizeof(url),
        "%s?latitude=%.6f&longitude=%.6f&hourly=wind_speed_10m,wind_gusts_10m,wind_direction_10m,cloud_cover,precipitation,is_day,temperature_2m&"
        "wind_speed_unit=kn&timezone=%s&models=%s%s&forecast_days=5",
        open_meteo_knmi_endpoint(), config->latitude, config->longitude, encoded_timezone,
        api_model, needs_fallback ? ",best_match" : "");
    if (written <= 0 || (size_t) written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_context_t response = {.body = calloc(1, OPEN_METEO_RESPONSE_LIMIT + 1)};
    if (!response.body) {
        s_diagnostics.allocation_failed = true;
        s_diagnostics.perform_result = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t http_config = {.url = url,
                                            .timeout_ms = OPEN_METEO_REQUEST_TIMEOUT_MS,
                                            .event_handler = response_event,
                                            .user_data = &response,
                                            .crt_bundle_attach = esp_crt_bundle_attach,
                                            .disable_auto_redirect = true};
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        free(response.body);
        return ESP_FAIL;
    }
    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    s_diagnostics.perform_result = result;
    s_diagnostics.http_status = status;
    s_diagnostics.response_length = response.length;
    s_diagnostics.too_large = response.too_large;
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status != 200 || response.too_large || response.length == 0) {
        ESP_LOGW("wind_provider", "Forecast download failed: transport=%d http=%d bytes=%u too_large=%d",
                 (int)result, status, (unsigned)response.length, response.too_large);
        free(response.body);
        return response.too_large ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
    }
    wind_local_datetime_t local;
    char first_date[WIND_FORECAST_DATE_LENGTH];
    if (wind_timezone_from_unix(config->timezone, retrieved_at, &local) != ESP_OK ||
        wind_timezone_format_date(&local, first_date, sizeof(first_date)) != ESP_OK) {
        free(response.body);
        return ESP_ERR_INVALID_STATE;
    }
    result = open_meteo_knmi_parse_json(config, response.body, response.length, retrieved_at,
                                        first_date, out_forecast);
    s_diagnostics.parse_result = result;
    if (result != ESP_OK) {
        ESP_LOGW("wind_provider", "Forecast parse failed: result=%d bytes=%u date=%s model=%s",
                 (int)result, (unsigned)response.length, first_date, config->model);
    }
    free(response.body);
    return result;
}
#else
static esp_err_t fetch_forecast(void *context, int64_t retrieved_at, wind_forecast_t *out_forecast)
{
    (void) context;
    (void) retrieved_at;
    (void) out_forecast;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

void open_meteo_knmi_provider_init(wind_provider_t *provider, open_meteo_knmi_config_t *config)
{
    if (provider) {
        provider->fetch = fetch_forecast;
        provider->context = config;
    }
}
