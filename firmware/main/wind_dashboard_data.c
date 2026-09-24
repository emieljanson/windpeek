#include "wind_dashboard_data.h"

#include <string.h>
#include "wind_timezone.h"

const char *wind_dashboard_day_name(int weekday) {
    static const char *names[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
                                  "THURSDAY", "FRIDAY", "SATURDAY"};
    return weekday >= 0 && weekday < 7 ? names[weekday] : "";
}

wind_renderer_sample_t wind_dashboard_forecast_sample(const wind_forecast_sample_t *source,
                                                       const char *time, bool available) {
    return (wind_renderer_sample_t){
        .time = time,
        .sustained_kt = source->wind_knots,
        .gust_kt = source->gust_knots,
        .destination_degrees = source->destination_degrees,
        .available = available,
        .weather = (wind_renderer_weather_t)wind_forecast_weather_state(source),
        .temperature_tenths_c = source->temperature_tenths_c,
        .temperature_available = source->temperature_available,
    };
}

wind_renderer_swell_sample_t wind_dashboard_swell_sample(const wind_swell_sample_t *source,
                                                         bool secondary) {
    return secondary
        ? (wind_renderer_swell_sample_t){source->secondary_height_cm,
                                         source->secondary_period_tenths,
                                         source->secondary_destination_degrees}
        : (wind_renderer_swell_sample_t){source->height_cm,
                                         source->period_tenths,
                                         source->destination_degrees};
}

esp_err_t wind_dashboard_build_overview_row(const char *spot_name, const char *timezone,
                                            const wind_forecast_t *forecast,
                                            const wind_swell_t *swell, bool swell_primary,
                                            time_t now, wind_renderer_dashboard_t *out) {
    if (!spot_name || !timezone || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->spot_name = spot_name;
    out->swell_size = swell_primary ? 2 : 0;
    out->wind_size = swell_primary ? 0 : 2;
    out->display_mode = WIND_RENDERER_MODE_SOLID;
    for (int day = 0; day < 5; ++day) {
        for (int hour = 0; hour < 24; ++hour)
            out->swell_hourly[day][hour] = out->secondary_swell_hourly[day][hour] = -1;
        for (int sample = 0; sample < 5; ++sample)
            out->swell[day][sample] = (wind_renderer_swell_sample_t){-1, -1, -1};
    }
    wind_local_datetime_t date;
    if (wind_timezone_from_unix(timezone, now, &date) != ESP_OK) return ESP_ERR_INVALID_STATE;
    const int hours[] = {8, 11, 14, 17, 20};
    for (int day = 0; day < 5; ++day) {
        out->days[day].day = day == 0 ? "TODAY" : wind_dashboard_day_name(wind_timezone_weekday(&date));
        for (int hour = 0; hour < 24; ++hour) {
            date.hour = hour;
            date.minute = date.second = 0;
            int64_t timestamp;
            if (wind_timezone_to_unix(timezone, &date, &timestamp) != ESP_OK) continue;
            int slot = -1;
            for (int index = 0; index < 5; ++index)
                if (hours[index] == hour) slot = index;
            if (slot >= 0 && forecast)
                for (int forecast_day = 0; forecast_day < 5; ++forecast_day)
                    for (int sample_index = 0; sample_index < 5; ++sample_index) {
                        const wind_forecast_sample_t *sample = &forecast->days[forecast_day].samples[sample_index];
                        if (sample->timestamp == timestamp)
                            out->days[day].samples[slot] = wind_dashboard_forecast_sample(sample, NULL, true);
                    }
            if (!swell_primary || !swell) continue;
            for (size_t index = 0; index < swell->sample_count; ++index) {
                const wind_swell_sample_t *sample = &swell->samples[index];
                if (sample->timestamp != timestamp) continue;
                out->swell_hourly[day][hour] = sample->height_cm;
                out->secondary_swell_hourly[day][hour] = sample->secondary_height_cm;
                if (slot >= 0) out->swell[day][slot] = wind_dashboard_swell_sample(sample, false);
            }
        }
        wind_timezone_shift_date(&date, 1);
    }
    return ESP_OK;
}
