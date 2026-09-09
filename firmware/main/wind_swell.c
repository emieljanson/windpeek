#include "wind_swell.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

// The saved GFS id remains stable; select the finest grid this provider exposes.
const char *wind_swell_preferred_model(const char *model, double latitude) {
    if (!model) return "best_match";
    return !strcmp(model, "ncep_gfswave025") && latitude >= -15 && latitude <= 52.5
        ? "ncep_gfswave016" : model;
}

void wind_swell_overlay(wind_swell_t *base, const wind_swell_t *preferred) {
    size_t p = 0;
    for (size_t b = 0; b < base->sample_count; ++b) {
        while (p < preferred->sample_count && preferred->samples[p].timestamp < base->samples[b].timestamp) ++p;
        if (p == preferred->sample_count) break;
        const wind_swell_sample_t *s = &preferred->samples[p];
        if (s->timestamp == base->samples[b].timestamp &&
            (s->height_cm == 0 || (s->height_cm > 0 && s->period_tenths > 0 && s->destination_degrees >= 0))) {
            base->samples[b] = *s;
        }
    }
}

bool wind_swell_validate(const wind_swell_t *swell) {
    if (!swell || !swell->spot_id[0] || !memchr(swell->spot_id, 0, sizeof(swell->spot_id)) ||
        !swell->timezone[0] || !memchr(swell->timezone, 0, sizeof(swell->timezone)) ||
        !memchr(swell->model, 0, sizeof(swell->model)) ||
        (strcmp(swell->model, "best_match") && strcmp(swell->model, "meteofrance_wave") && strcmp(swell->model, "ncep_gfswave025") && strcmp(swell->model, "dwd_ewam")) ||
        swell->retrieved_at <= 0 || swell->sample_count == 0 || swell->sample_count > WIND_SWELL_MAX_SAMPLES) return false;
    int64_t previous = 0;
    for (size_t i = 0; i < swell->sample_count; ++i) {
        const wind_swell_sample_t *s = &swell->samples[i];
        if (s->timestamp <= previous || s->height_cm < -1 || s->height_cm > 10000 ||
            s->period_tenths < -1 || s->period_tenths > 1000 || s->destination_degrees < -1 || s->destination_degrees >= 360 ||
            s->secondary_height_cm < -1 || s->secondary_height_cm > 10000 ||
            s->secondary_period_tenths < -1 || s->secondary_period_tenths > 1000 ||
            s->secondary_destination_degrees < -1 || s->secondary_destination_degrees >= 360) return false;
        previous = s->timestamp;
    }
    return true;
}

esp_err_t wind_swell_parse(const open_meteo_marine_config_t *config, const char *json, size_t length, int64_t now, wind_swell_t *out) {
    if (!open_meteo_marine_config_valid(config) || !json || !out || !length || length > OPEN_METEO_MARINE_RESPONSE_LIMIT || now <= 0) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!root) return ESP_ERR_INVALID_ARG;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    const char *fields[] = { "time", "swell_wave_height", "swell_wave_period", "swell_wave_direction" };
    const char *units[] = { "unixtime", "m", "s", "°" };
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    const cJSON *unit_values = cJSON_GetObjectItemCaseSensitive(root, "hourly_units");
    const cJSON *timezone = cJSON_GetObjectItemCaseSensitive(root, "timezone");
    const cJSON *values[4];
    const char *secondary_fields[] = { "secondary_swell_wave_height", "secondary_swell_wave_period", "secondary_swell_wave_direction" };
    const cJSON *secondary_values[3] = {NULL, NULL, NULL};
    bool secondary_valid = true;
    int count = 0;
    if (!cJSON_IsString(timezone) || strcmp(timezone->valuestring, config->timezone)) goto done;
    for (int f = 0; f < 4; ++f) {
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(unit_values, fields[f]);
        const cJSON *array = cJSON_GetObjectItemCaseSensitive(hourly, fields[f]);
        if (!cJSON_IsString(unit) || strcmp(unit->valuestring, units[f]) || !cJSON_IsArray(array)) goto done;
        if (f == 0) count = cJSON_GetArraySize(array);
        if (count < 1 || count > WIND_SWELL_MAX_SAMPLES || cJSON_GetArraySize(array) != count) goto done;
        values[f] = array->child;
    }
    for (int f = 0; f < 3; ++f) {
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(unit_values, secondary_fields[f]);
        const cJSON *array = cJSON_GetObjectItemCaseSensitive(hourly, secondary_fields[f]);
        if (!cJSON_IsString(unit) || strcmp(unit->valuestring, units[f + 1]) ||
            !cJSON_IsArray(array) || cJSON_GetArraySize(array) != count) secondary_valid = false;
        else secondary_values[f] = array->child;
    }
    memset(out, 0, sizeof(*out));
    if (strlen(config->spot_id) >= sizeof(out->spot_id) || strlen(config->timezone) >= sizeof(out->timezone)) goto done;
    strcpy(out->spot_id, config->spot_id);
    strcpy(out->timezone, config->timezone);
    snprintf(out->model, sizeof(out->model), "%s", config->swell_model ? config->swell_model : "best_match");
    out->retrieved_at = now;
    out->sample_count = count;
    for (int i = 0; i < count; ++i) {
        if (!cJSON_IsNumber(values[0]) || !isfinite(values[0]->valuedouble) || values[0]->valuedouble < 1 || values[0]->valuedouble > 4102444800.0 || floor(values[0]->valuedouble) != values[0]->valuedouble) goto done;
        wind_swell_sample_t *s = &out->samples[i];
        s->timestamp = (int64_t)values[0]->valuedouble;
        double h = values[1]->valuedouble, p = values[2]->valuedouble, d = values[3]->valuedouble;
        s->height_cm = cJSON_IsNumber(values[1]) && isfinite(h) && h >= 0 && h <= 100 ? (int)lround(h * 100) : -1;
        s->period_tenths = cJSON_IsNumber(values[2]) && isfinite(p) && p > 0 && p <= 100 ? (int)lround(p * 10) : -1;
        s->destination_degrees = cJSON_IsNumber(values[3]) && isfinite(d) && d >= 0 && d <= 360 ? (int)lround(d + 180) % 360 : -1;
        s->secondary_height_cm = s->secondary_period_tenths = s->secondary_destination_degrees = -1;
        if (secondary_valid) {
            h = secondary_values[0]->valuedouble; p = secondary_values[1]->valuedouble; d = secondary_values[2]->valuedouble;
            s->secondary_height_cm = cJSON_IsNumber(secondary_values[0]) && isfinite(h) && h >= 0 && h <= 100 ? (int)lround(h * 100) : -1;
            s->secondary_period_tenths = cJSON_IsNumber(secondary_values[1]) && isfinite(p) && p > 0 && p <= 100 ? (int)lround(p * 10) : -1;
            s->secondary_destination_degrees = cJSON_IsNumber(secondary_values[2]) && isfinite(d) && d >= 0 && d <= 360 ? (int)lround(d + 180) % 360 : -1;
            for (int f = 0; f < 3; ++f) secondary_values[f] = secondary_values[f]->next;
        }
        for (int f = 0; f < 4; ++f) values[f] = values[f]->next;
    }
    if (wind_swell_validate(out)) result = ESP_OK;
done:
    cJSON_Delete(root);
    return result;
}

#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
typedef struct {
    char *body;
    size_t length;
    bool too_large;
} response_t;

static esp_err_t response_event(esp_http_client_event_t *event)
{
    response_t *response = (response_t *) event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    if (response->length + (size_t) event->data_len > OPEN_METEO_MARINE_RESPONSE_LIMIT) {
        response->too_large = true;
        return ESP_FAIL;
    }
    memcpy(response->body + response->length, event->data, (size_t) event->data_len);
    response->length += (size_t) event->data_len;
    response->body[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t fetch_model(const open_meteo_marine_config_t *config, const char *model, int64_t retrieved_at, wind_swell_t *out_tide)
{
    if (!open_meteo_marine_config_valid(config) || !out_tide) return ESP_ERR_INVALID_STATE;
    char url[640];
    int written = snprintf(url, sizeof(url),
        "%s?latitude=%.6f&longitude=%.6f&hourly=swell_wave_height,swell_wave_period,swell_wave_direction,secondary_swell_wave_height,secondary_swell_wave_period,secondary_swell_wave_direction&timezone=%s&forecast_days=5&timeformat=unixtime&cell_selection=sea&models=%s",
        OPEN_METEO_MARINE_ENDPOINT, config->latitude, config->longitude, config->timezone, model);
    if (written <= 0 || (size_t) written >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    response_t response = {.body = calloc(1, OPEN_METEO_MARINE_RESPONSE_LIMIT + 1)};
    if (!response.body) return ESP_ERR_NO_MEM;
    esp_http_client_config_t http = {.url = url, .timeout_ms = OPEN_METEO_MARINE_TIMEOUT_MS,
                                     .event_handler = response_event, .user_data = &response,
                                     .crt_bundle_attach = esp_crt_bundle_attach,
                                     .disable_auto_redirect = true};
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client) {
        free(response.body);
        return ESP_FAIL;
    }
    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status != 200 || response.too_large || response.length == 0) {
        free(response.body);
        return response.too_large ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
    }
    result = wind_swell_parse(config, response.body, response.length, retrieved_at,
                                           out_tide);
    free(response.body);
    return result;
}
esp_err_t wind_swell_fetch(const open_meteo_marine_config_t *config, int64_t now, wind_swell_t *out) {
    if (!open_meteo_marine_config_valid(config) || !out) return ESP_ERR_INVALID_STATE;
    const char *selected_model = config->swell_model ? config->swell_model : "best_match";
    const char *base_model = !strcmp(selected_model, "dwd_ewam") ? "dwd_gwam" : selected_model;
    const char *preferred_model = wind_swell_preferred_model(selected_model, config->latitude);
    esp_err_t base_result = fetch_model(config, base_model, now, out);
    if (!strcmp(base_model, preferred_model)) return base_result;
    wind_swell_t *preferred = calloc(1, sizeof(*preferred));
    if (!preferred) return base_result == ESP_OK ? ESP_OK : ESP_ERR_NO_MEM;
    esp_err_t preferred_result = fetch_model(config, preferred_model, now, preferred);
    if (preferred_result == ESP_OK) {
        if (base_result == ESP_OK) wind_swell_overlay(out, preferred);
        else *out = *preferred;
    }
    free(preferred);
    return base_result == ESP_OK || preferred_result == ESP_OK ? ESP_OK : base_result;
}

#else
esp_err_t wind_swell_fetch(const open_meteo_marine_config_t *config, int64_t now, wind_swell_t *out) { (void)config; (void)now; (void)out; return ESP_ERR_NOT_SUPPORTED; }
#endif
