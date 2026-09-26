#include "wind_app_prefetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "open_meteo_knmi_provider.h"
#include "open_meteo_marine_provider.h"
#include "wind_swell_cache.h"
#include "wind_tide_cache.h"

static const char *TAG = "wind_app_prefetch";

void wind_app_prefetch_spot_capture(wind_app_prefetch_spot_t *snapshot,
                                    const wind_spot_runtime_t *runtime,
                                    const installed_display_configuration_t *display) {
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->app = runtime->app;
    snapshot->original_schedule = runtime->app.schedule;
    snapshot->provider = runtime->provider_config;
    snapshot->marine = runtime->marine_config;
    snprintf(snapshot->spot_id, sizeof(snapshot->spot_id), "%s", runtime->spot->id);
    snprintf(snapshot->spot_name, sizeof(snapshot->spot_name), "%s", runtime->spot->display_name);
    snprintf(snapshot->timezone, sizeof(snapshot->timezone), "%s", runtime->spot->timezone);
    snprintf(snapshot->model, sizeof(snapshot->model), "%s", runtime->app.config.identity.model);
    snprintf(snapshot->swell_model, sizeof(snapshot->swell_model), "%s",
             runtime->marine_config.swell_model);
    snprintf(snapshot->forecast_path, sizeof(snapshot->forecast_path), "%s", runtime->forecast_path);
    snprintf(snapshot->schedule_path, sizeof(snapshot->schedule_path), "%s", runtime->schedule_path);
    snprintf(snapshot->tide_path, sizeof(snapshot->tide_path), "%s", runtime->tide_path);
    snapshot->provider.spot_id = snapshot->app.config.identity.spot_id =
        snapshot->marine.spot_id = snapshot->spot_id;
    snapshot->provider.spot_name = snapshot->spot_name;
    snapshot->provider.timezone = snapshot->app.config.identity.timezone =
        snapshot->marine.timezone = snapshot->timezone;
    snapshot->provider.model = snapshot->app.config.identity.model = snapshot->model;
    snapshot->marine.swell_model = snapshot->swell_model;
    snapshot->app.config.forecast_cache_path = snapshot->forecast_path;
    snapshot->app.config.schedule_path = snapshot->schedule_path;
    snapshot->app.config.io_context = NULL; // Prefetch never renders or displays.
    open_meteo_knmi_provider_init(&snapshot->app.config.provider, &snapshot->provider);
    snapshot->show_swell = display->swell_size != 0;
    snapshot->show_tide = display->show_tide;
}

static bool prefetch_swell(const wind_app_prefetch_spot_t *spot, time_t now, bool force) {
    char path[128];
    snprintf(path, sizeof(path), "%s.swell", spot->forecast_path);
    const wind_swell_cache_identity_t identity = {
        spot->spot_id, spot->timezone, spot->swell_model};
    wind_swell_t *swell = malloc(sizeof(*swell));
    if (!swell) return false;
    const bool fresh = !force && wind_swell_cache_load(path, &identity, swell) == ESP_OK &&
        swell->retrieved_at <= now && now - swell->retrieved_at < 6 * 3600;
    const bool ready = fresh ||
        (wind_swell_fetch(&spot->marine, now, swell) == ESP_OK &&
         wind_swell_validate(swell) && wind_swell_cache_store(path, swell) == ESP_OK);
    free(swell);
    return ready;
}

static bool prefetch_tide(wind_app_prefetch_spot_t *spot, time_t now, bool force) {
    const wind_tide_cache_identity_t identity = {spot->spot_id, spot->timezone};
    wind_tide_t *tide = malloc(sizeof(*tide));
    if (!tide) return false;
    const bool fresh = !force && wind_tide_cache_load(spot->tide_path, &identity, tide) == ESP_OK &&
        tide->retrieved_at <= now && now - tide->retrieved_at < 6 * 3600;
    bool ready = fresh;
    if (!fresh) {
        wind_tide_provider_t provider;
        open_meteo_marine_provider_init(&provider, &spot->marine);
        ready = provider.fetch && provider.fetch(provider.context, now, tide) == ESP_OK &&
            wind_tide_validate(tide) && wind_tide_cache_store(spot->tide_path, tide) == ESP_OK;
    }
    free(tide);
    return ready;
}

bool wind_app_prefetch_spot_fetch(wind_app_prefetch_spot_t *spot, time_t now,
                                  bool force_refresh) {
    wind_app_outcome_t outcome = {0};
    const bool retry_due = wind_schedule_retry_is_due(&spot->app.schedule, now, NULL);
    esp_err_t result = wind_app_prefetch(&spot->app, force_refresh, now, &outcome);
    // The background worker retries every five minutes. Once the ordinary
    // schedule's single retry is spent, a missing cache still needs recovery.
    if (result == ESP_OK && !outcome.attempted_fetch && !outcome.used_cache &&
        !outcome.published_forecast && spot->app.schedule.retry_at <= now)
        result = wind_app_prefetch(&spot->app, true, now, &outcome);
    if (result != ESP_OK || (outcome.attempted_fetch && outcome.fetch_result != ESP_OK))
        ESP_LOGW(TAG, "Background forecast for %s failed: %s", spot->spot_id,
                 esp_err_to_name(result != ESP_OK ? result : outcome.fetch_result));
    // A new forecast window needs matching marine data, even just after midnight.
    const bool refresh_marine = force_refresh || outcome.published_forecast;
    const bool swell_ready = !spot->show_swell || prefetch_swell(spot, now, refresh_marine);
    const bool tide_ready = !spot->show_tide || prefetch_tide(spot, now, refresh_marine);
    const bool ready = result == ESP_OK && outcome.fetch_result == ESP_OK &&
        (outcome.used_cache || outcome.published_forecast) &&
        swell_ready && tide_ready;
    // A forced round can fail before a spot's own schedule is due. Marine
    // failures also deserve the same single retry as a failed wind request.
    if (!ready && !retry_due && spot->app.schedule.retry_at == 0) {
        const int64_t boundary = wind_schedule_latest_boundary(spot->timezone, now);
        wind_schedule_schedule_retry(&spot->app.schedule, boundary, now + 5 * 60);
        wind_schedule_state_store(spot->schedule_path, &spot->app.schedule);
    }
    if ((!swell_ready || !tide_ready) && outcome.fetch_result == ESP_OK) {
        outcome.attempted_fetch = true;
        outcome.fetch_result = ESP_FAIL;
    }
    spot->outcome = outcome;
    return ready;
}
