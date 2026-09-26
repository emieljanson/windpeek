#include "wind_app.h"
#include "wind_app_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wind_timezone.h"

static const int64_t FAILED_REFRESH_RETRY_SECONDS = 5 * 60;

wind_freshness_t wind_app_forecast_freshness(const wind_forecast_t *forecast, int64_t now) {
    if (!forecast) {
        return WIND_FRESHNESS_UNAVAILABLE;
    }
    int64_t age = now - forecast->retrieved_at;
    if (age < -5 * 60) {
        return WIND_FRESHNESS_UNAVAILABLE;
    }
    if (age <= 0 || age < 6 * 60 * 60) {
        return WIND_FRESHNESS_FRESH;
    }
    return age >= 24 * 60 * 60 ? WIND_FRESHNESS_STALE : WIND_FRESHNESS_AGED;
}

bool wind_app_forecast_covers_window(const wind_forecast_t *forecast,
                                             const char *timezone, int64_t now) {
    if (!forecast || !timezone || now <= 0 || !wind_forecast_validate(forecast)) {
        return false;
    }
    wind_local_datetime_t local;
    char today[WIND_FORECAST_DATE_LENGTH];
    if (wind_timezone_from_unix(timezone, now, &local) != ESP_OK ||
        wind_timezone_format_date(&local, today, sizeof(today)) != ESP_OK) {
        return false;
    }
    return strcmp(forecast->days[0].local_date, today) == 0;
}

esp_err_t wind_app_init(wind_app_t *app, const wind_app_config_t *config) {
    if (!app || !config || !config->provider.fetch || !config->identity.spot_id ||
        !config->identity.timezone || !config->identity.model ||
        !config->forecast_cache_path || !config->panel_cache_path ||
        !config->schedule_path || !config->render || !config->display ||
        config->render_signature == 0 || config->bitmap_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(app, 0, sizeof(*app));
    app->config = *config;
    if (wind_schedule_state_load_scoped(config->schedule_path, config->identity.spot_id,
                                        config->identity.timezone,
                                        &app->schedule) != ESP_OK) {
        if (wind_schedule_state_set_scope(&app->schedule, config->identity.spot_id,
                                          config->identity.timezone) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    app->initialized = true;
    return ESP_OK;
}

typedef enum {
    REFRESH_DISPLAY,
    PREFETCH_ONLY,
    DISPLAY_CACHED,
    DISPLAY_PREFETCHED,
    VERIFY_SETUP,
} refresh_mode_t;

static esp_err_t run_internal(wind_app_t *app, refresh_mode_t mode,
                              bool force_refresh, int64_t now,
                              wind_app_outcome_t *outcome) {
    if (!app || !app->initialized || now <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    wind_app_outcome_t local = mode == DISPLAY_PREFETCHED && outcome
        ? *outcome : (wind_app_outcome_t){.fetch_result = ESP_OK};
    wind_forecast_t active;
    bool have_active = wind_cache_load(app->config.forecast_cache_path,
                                       &app->config.identity, &active) == ESP_OK;
    local.used_cache = have_active;
    if ((mode == REFRESH_DISPLAY || mode == PREFETCH_ONLY) && !have_active &&
        app->schedule.last_satisfied_boundary > 0 &&
        wind_schedule_state_set_scope(&app->schedule, app->config.identity.spot_id,
                                      app->config.identity.timezone) != ESP_OK)
        return ESP_ERR_INVALID_STATE;

    bool cache_has_coverage =
        have_active &&
        wind_app_forecast_covers_window(&active, app->config.identity.timezone, now);
    bool coverage_refresh_due =
        have_active && !cache_has_coverage &&
        (!app->coverage_refresh_attempted ||
         app->coverage_refresh_cache_retrieved_at != active.retrieved_at);
    int64_t boundary = 0;
    bool due = wind_schedule_is_due(&app->schedule, (time_t)now, &boundary);
    int64_t retry_boundary = 0;
    bool pending_retry_due =
        wind_schedule_retry_is_due(&app->schedule, (time_t)now, &retry_boundary);
    // A regular boundary supersedes a missed retry. Treat this as a fresh
    // scheduled attempt so a transient failure still earns its own one retry.
    bool retry_due = pending_retry_due && !due;
    bool initial_fetch_due = !have_active && app->schedule.last_attempted_boundary == 0;
    const bool may_fetch = mode != DISPLAY_CACHED && mode != DISPLAY_PREFETCHED;
    if (may_fetch && (force_refresh || due || coverage_refresh_due || retry_due ||
                     initial_fetch_due)) {
        if (coverage_refresh_due) {
            app->coverage_refresh_attempted = true;
            app->coverage_refresh_cache_retrieved_at = active.retrieved_at;
        }
        if (due) {
            wind_schedule_mark_attempted(&app->schedule, boundary);
            if (pending_retry_due) {
                wind_schedule_consume_retry(&app->schedule);
            }
            if (wind_schedule_state_store(app->config.schedule_path, &app->schedule) !=
                ESP_OK) {
                if (outcome) {
                    *outcome = local;
                }
                return ESP_FAIL;
            }
        }
        if (retry_due) {
            boundary = retry_boundary;
            wind_schedule_consume_retry(&app->schedule);
            if (wind_schedule_state_store(app->config.schedule_path, &app->schedule) !=
                ESP_OK) {
                if (outcome) {
                    *outcome = local;
                }
                return ESP_FAIL;
            }
        }
        wind_forecast_t fetched;
        wind_forecast_clear(&fetched);
        local.attempted_fetch = true;
        local.fetch_result = wind_provider_fetch(&app->config.provider, now, &fetched);
        if (local.fetch_result == ESP_OK && wind_forecast_validate(&fetched) &&
            wind_app_forecast_covers_window(&fetched, app->config.identity.timezone,
                                             now)) {
            local.fetch_result =
                wind_cache_store(app->config.forecast_cache_path, &fetched);
            if (local.fetch_result == ESP_OK) {
                active = fetched;
                have_active = true;
                local.used_cache = false;
                local.published_forecast = true;
                app->coverage_refresh_attempted = false;
                app->coverage_refresh_cache_retrieved_at = 0;
                wind_schedule_consume_retry(&app->schedule);
                wind_schedule_mark_satisfied(&app->schedule, boundary);
                wind_schedule_state_store(app->config.schedule_path, &app->schedule);
            }
        } else if (local.fetch_result == ESP_OK) {
            local.fetch_result = ESP_ERR_INVALID_RESPONSE;
        }
        const bool automatic_attempt = due || coverage_refresh_due || initial_fetch_due;
        if (local.fetch_result != ESP_OK && automatic_attempt && !retry_due) {
            wind_schedule_schedule_retry(&app->schedule, boundary,
                                         now + FAILED_REFRESH_RETRY_SECONDS);
            wind_schedule_state_store(app->config.schedule_path, &app->schedule);
        }
    }

    local.freshness = wind_app_forecast_freshness(have_active ? &active : NULL, now);
    // Installation must prove the new configuration can fetch real data
    // before touching the panel. Ordinary offline refreshes may show cache.
    if (mode == VERIFY_SETUP && (!local.published_forecast ||
                            local.freshness == WIND_FRESHNESS_UNAVAILABLE)) {
        if (outcome) *outcome = local;
        return local.fetch_result != ESP_OK ? local.fetch_result : ESP_ERR_INVALID_RESPONSE;
    }
    if (mode == PREFETCH_ONLY) {
        if (outcome) {
            *outcome = local;
        }
        return ESP_OK;
    }
    uint8_t *bitmap = (uint8_t *)malloc(app->config.bitmap_size);
    if (!bitmap) {
        return ESP_ERR_NO_MEM;
    }
    const bool refresh_failed = local.attempted_fetch && local.fetch_result != ESP_OK;
    esp_err_t result = app->config.render(
        app->config.io_context, have_active ? &active : NULL, local.freshness,
        refresh_failed, now, bitmap, app->config.bitmap_size);
    if (result != ESP_OK) {
        free(bitmap);
        if (outcome) {
            *outcome = local;
        }
        return result;
    }

    uint64_t hash = wind_cache_bitmap_hash(bitmap, app->config.bitmap_size);
    uint64_t confirmed_hash = 0;
    if (!app->force_display && hash != 0 &&
        wind_cache_panel_load(app->config.panel_cache_path,
                              app->config.render_signature,
                              &confirmed_hash) == ESP_OK &&
        hash == confirmed_hash) {
        local.display_unchanged = true;
        free(bitmap);
        if (outcome) {
            *outcome = local;
        }
        return ESP_OK;
    }

    result =
        app->config.display(app->config.io_context, bitmap, app->config.bitmap_size);
    free(bitmap);
    if (result != ESP_OK) {
        wind_cache_panel_invalidate(app->config.panel_cache_path);
        if (outcome) {
            *outcome = local;
        }
        return result;
    }
    result = wind_cache_panel_confirm(app->config.panel_cache_path,
                                      app->config.render_signature, hash);
    if (result == ESP_OK) {
        local.displayed = true;
        app->force_display = false;
    }
    if (outcome) {
        *outcome = local;
    }
    return result;
}

esp_err_t wind_app_run(wind_app_t *app, bool force_refresh, int64_t now,
                       wind_app_outcome_t *outcome) {
    return run_internal(app, REFRESH_DISPLAY, force_refresh, now, outcome);
}

esp_err_t wind_app_prefetch(wind_app_t *app, bool force_refresh, int64_t now,
                            wind_app_outcome_t *outcome) {
    return run_internal(app, PREFETCH_ONLY, force_refresh, now, outcome);
}

esp_err_t wind_app_run_setup(wind_app_t *app, int64_t now, wind_app_outcome_t *outcome) {
    return run_internal(app, VERIFY_SETUP, true, now, outcome);
}

esp_err_t wind_app_show_cached(wind_app_t *app, int64_t now,
                               wind_app_outcome_t *outcome) {
    return run_internal(app, DISPLAY_CACHED, false, now, outcome);
}

esp_err_t wind_app_show_prefetched(wind_app_t *app, int64_t now,
                                  wind_app_outcome_t *outcome) {
    return run_internal(app, DISPLAY_PREFETCHED, false, now, outcome);
}
