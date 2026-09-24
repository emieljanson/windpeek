#include "wind_renderer.h"
#include "wind_renderer_internal.h"

#include <string.h>

static const char *safe_text(const char *text) { return text ? text : ""; }
static int text_fits(const char *text, size_t capacity) {
    return !text || memchr(text, '\0', capacity) != NULL;
}
static int copy_bounded_text(char *destination, size_t capacity, const char *source) {
    const char *text = safe_text(source);
    if (!destination || capacity == 0 || !text_fits(text, capacity)) return -1;
    const size_t length = strlen(text);
    memcpy(destination, text, length + 1);
    return 0;
}

uint32_t wind_renderer_contract_version(void) {
    return WIND_RENDERER_CONTRACT_VERSION;
}

void wind_renderer_input_v2_init(wind_renderer_input_v2_t *input) {
    if (!input) return;
    memset(input, 0, sizeof(*input));
    input->version = WIND_RENDERER_CONTRACT_VERSION;
    input->battery_percent = -1;
    input->threshold_kt = WIND_RENDERER_DEFAULT_THRESHOLD_KT;
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        for (int hour = 0; hour < 24; ++hour) {
            input->swell_hourly[day][hour] = -1;
            input->secondary_swell_hourly[day][hour] = -1;
        }
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample)
            input->secondary_swell[day][sample] = (wind_renderer_swell_sample_t){-1, -1, -1};
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample)
            input->swell[day][sample] = (wind_renderer_swell_sample_t){-1, -1, -1};
    }
    input->show_weather = 1;
    input->show_dedicated_footer = 1;
}

int wind_renderer_input_v2_set_metadata(wind_renderer_input_v2_t *input,
                                        const char *spot_name, const char *provider,
                                        const char *updated_time) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION ||
        !text_fits(safe_text(spot_name), sizeof(input->spot_name)) ||
        !text_fits(safe_text(provider), sizeof(input->provider)) ||
        !text_fits(safe_text(updated_time), sizeof(input->updated_time))) {
        return -1;
    }
    copy_bounded_text(input->spot_name, sizeof(input->spot_name), spot_name);
    copy_bounded_text(input->provider, sizeof(input->provider), provider);
    copy_bounded_text(input->updated_time, sizeof(input->updated_time), updated_time);
    return 0;
}

int wind_renderer_input_v2_set_status(wind_renderer_input_v2_t *input,
                                      wind_renderer_state_t state, int refresh_failed,
                                      int age_hours, int battery_percent,
                                      wind_renderer_display_mode_t display_mode,
                                      int threshold_kt) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION ||
        state < WIND_RENDERER_FRESH || state > WIND_RENDERER_UNAVAILABLE ||
        (refresh_failed != 0 && refresh_failed != 1) || age_hours < 0 ||
        battery_percent < -1 || battery_percent > 100 ||
        (display_mode != WIND_RENDERER_MODE_THRESHOLD &&
         display_mode != WIND_RENDERER_MODE_SOLID) ||
        threshold_kt < WIND_RENDERER_MIN_THRESHOLD_KT ||
        threshold_kt > WIND_RENDERER_MAX_THRESHOLD_KT) {
        return -1;
    }
    input->state = state;
    input->refresh_failed = refresh_failed;
    input->age_hours = age_hours;
    input->battery_percent = battery_percent;
    input->display_mode = display_mode;
    input->threshold_kt = threshold_kt;
    return 0;
}

int wind_renderer_input_v2_set_display_rows(wind_renderer_input_v2_t *input,
                                            int show_weather, int show_temperature,
                                            int show_tide, int tide_available) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION ||
        (show_weather != 0 && show_weather != 1) ||
        (show_temperature != 0 && show_temperature != 1) ||
        (show_tide != 0 && show_tide != 1) ||
        (tide_available != 0 && tide_available != 1))
        return -1;
    input->show_weather = show_weather;
    input->show_temperature = show_temperature;
    input->show_tide = show_tide;
    input->tide_available = tide_available;
    return 0;
}

int wind_renderer_input_v2_set_swell(wind_renderer_input_v2_t *input,
    int day_index, int sample_index, int height_cm, int period_tenths, int destination_degrees) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION ||
        day_index < 0 || day_index >= WIND_RENDERER_DAY_COUNT ||
        sample_index < 0 || sample_index >= WIND_RENDERER_SAMPLES_PER_DAY ||
        height_cm < -1 || height_cm > 10000 || period_tenths < -1 || period_tenths > 1000 ||
        destination_degrees < -1 || destination_degrees >= 360) return -1;
    if (!input->custom_modules) { input->wind_size = 1; input->swell_size = 2; }
    input->custom_modules = 1;
    input->swell[day_index][sample_index] = (wind_renderer_swell_sample_t){
        height_cm, period_tenths, destination_degrees};
    return 0;
}

int wind_renderer_input_v2_set_preferences(wind_renderer_input_v2_t *input,
                                           int use_24_hour, int temperature_fahrenheit,
                                           int show_dedicated_footer) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION ||
        (use_24_hour != 0 && use_24_hour != 1) ||
        (temperature_fahrenheit != 0 && temperature_fahrenheit != 1) ||
        (show_dedicated_footer != 0 && show_dedicated_footer != 1)) {
        return -1;
    }
    input->use_24_hour = use_24_hour;
    input->temperature_fahrenheit = temperature_fahrenheit;
    input->show_dedicated_footer = show_dedicated_footer;
    return 0;
}

int wind_renderer_input_v2_set_day(wind_renderer_input_v2_t *input, int day_index,
                                   const char *day, const char *date) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION || day_index < 0 ||
        day_index >= WIND_RENDERER_DAY_COUNT ||
        !text_fits(safe_text(day), sizeof(input->days[day_index].day)) ||
        !text_fits(safe_text(date), sizeof(input->days[day_index].date))) {
        return -1;
    }
    copy_bounded_text(input->days[day_index].day, sizeof(input->days[day_index].day),
                      day);
    copy_bounded_text(input->days[day_index].date, sizeof(input->days[day_index].date),
                      date);
    return 0;
}

int wind_renderer_input_v2_set_sample(wind_renderer_input_v2_t *input, int day_index,
                                      int sample_index, const char *time,
                                      int sustained_kt, int gust_kt,
                                      int destination_degrees, int available,
                                      wind_renderer_weather_t weather,
                                      int temperature_tenths_c,
                                      int temperature_available) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION || day_index < 0 ||
        day_index >= WIND_RENDERER_DAY_COUNT || sample_index < 0 ||
        sample_index >= WIND_RENDERER_SAMPLES_PER_DAY ||
        (available != 0 && available != 1) ||
        (temperature_available != 0 && temperature_available != 1) ||
        weather < WIND_RENDERER_WEATHER_UNAVAILABLE ||
        weather > WIND_RENDERER_WEATHER_HEAVY_RAIN ||
        !text_fits(safe_text(time),
                   sizeof(input->days[day_index].samples[sample_index].time)) ||
        (available && (!time || !time[0]))) {
        return -1;
    }
    wind_renderer_input_sample_v2_t *sample =
        &input->days[day_index].samples[sample_index];
    copy_bounded_text(sample->time, sizeof(sample->time), time);
    sample->sustained_kt = sustained_kt;
    sample->gust_kt = gust_kt;
    sample->destination_degrees = destination_degrees;
    sample->available = available;
    sample->weather = weather;
    sample->temperature_tenths_c = temperature_tenths_c;
    sample->temperature_available = temperature_available;
    return 0;
}

int wind_renderer_input_v2_set_tide_sample(wind_renderer_input_v2_t *input,
                                           int tide_index, int day_index,
                                           int local_hour, int sea_level_mm,
                                           int available) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION || tide_index < 0 ||
        tide_index >= WIND_RENDERER_MAX_TIDE_SAMPLES || day_index < 0 ||
        day_index >= WIND_RENDERER_DAY_COUNT || local_hour < 0 || local_hour > 23 ||
        (available != 0 && available != 1))
        return -1;
    input->tide_samples[tide_index] = (wind_renderer_input_tide_sample_v2_t){
        .day_index = day_index,
        .local_hour = local_hour,
        .sea_level_mm = sea_level_mm,
        .available = available,
    };
    if (tide_index >= input->tide_sample_count)
        input->tide_sample_count = tide_index + 1;
    return 0;
}

int wind_renderer_input_v2_set_tide_extremum(wind_renderer_input_v2_t *input,
                                             int extremum_index, int day_index,
                                             int local_hour, int local_minute,
                                             int sea_level_mm, int is_high,
                                             int available) {
    if (!input || input->version != WIND_RENDERER_CONTRACT_VERSION ||
        extremum_index < 0 || extremum_index >= WIND_RENDERER_MAX_TIDE_EXTREMA ||
        day_index < 0 || day_index >= WIND_RENDERER_DAY_COUNT || local_hour < 0 ||
        local_hour > 23 || local_minute < 0 || local_minute > 59 ||
        local_minute % 15 != 0 || (is_high != 0 && is_high != 1) ||
        (available != 0 && available != 1))
        return -1;
    input->tide_extrema[extremum_index] = (wind_renderer_input_tide_extremum_v2_t){
        .day_index = day_index,
        .local_hour = local_hour,
        .local_minute = local_minute,
        .sea_level_mm = sea_level_mm,
        .is_high = is_high,
        .available = available,
    };
    if (extremum_index >= input->tide_extremum_count)
        input->tide_extremum_count = extremum_index + 1;
    return 0;
}

int wind_renderer_input_v2_to_dashboard(const wind_renderer_input_v2_t *input,
                                        wind_renderer_dashboard_t *dashboard) {
    if (!input || !dashboard || input->version != WIND_RENDERER_CONTRACT_VERSION)
        return -1;

    wind_renderer_dashboard_t result = {0};
    result.spot_name = input->spot_name;
    result.provider = input->provider;
    result.updated_time = input->updated_time;
    result.state = (wind_renderer_state_t)input->state;
    result.refresh_failed = input->refresh_failed;
    result.age_hours = input->age_hours;
    result.battery_percent = input->battery_percent;
    result.display_mode = (wind_renderer_display_mode_t)input->display_mode;
    result.threshold_kt = input->threshold_kt;
    result.custom_modules = input->custom_modules;
    result.wind_size = input->wind_size;
    result.swell_size = input->swell_size;
    memcpy(result.swell_hourly, input->swell_hourly, sizeof(result.swell_hourly));
    memcpy(result.secondary_swell_hourly, input->secondary_swell_hourly, sizeof(result.secondary_swell_hourly));
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day)
        memcpy(result.secondary_swell[day], input->secondary_swell[day], sizeof(input->secondary_swell[day]));
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day)
        memcpy(result.swell[day], input->swell[day], sizeof(input->swell[day]));
    result.show_weather = input->show_weather;
    result.show_temperature = input->show_temperature;
    result.show_tide = input->show_tide;
    result.show_dedicated_footer = input->show_dedicated_footer;
    result.use_24_hour = input->use_24_hour;
    result.temperature_fahrenheit = input->temperature_fahrenheit;
    result.tide_available = input->tide_available;
    result.tide_sample_count = input->tide_sample_count;
    for (int index = 0; index < input->tide_sample_count; ++index) {
        result.tide_samples[index] = (wind_renderer_tide_sample_t){
            .day_index = input->tide_samples[index].day_index,
            .local_hour = input->tide_samples[index].local_hour,
            .sea_level_mm = input->tide_samples[index].sea_level_mm,
            .available = input->tide_samples[index].available,
        };
    }
    result.tide_extremum_count = input->tide_extremum_count;
    for (int index = 0; index < input->tide_extremum_count; ++index) {
        result.tide_extrema[index] = (wind_renderer_tide_extremum_t){
            .day_index = input->tide_extrema[index].day_index,
            .local_hour = input->tide_extrema[index].local_hour,
            .local_minute = input->tide_extrema[index].local_minute,
            .sea_level_mm = input->tide_extrema[index].sea_level_mm,
            .is_high = input->tide_extrema[index].is_high,
            .available = input->tide_extrema[index].available,
        };
    }
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        result.days[day].day = input->days[day].day;
        result.days[day].date = input->days[day].date;
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            const wind_renderer_input_sample_v2_t *source =
                &input->days[day].samples[sample];
            result.days[day].samples[sample] = (wind_renderer_sample_t){
                .time = source->time,
                .sustained_kt = source->sustained_kt,
                .gust_kt = source->gust_kt,
                .destination_degrees = source->destination_degrees,
                .available = source->available,
                .weather = (wind_renderer_weather_t)source->weather,
                .temperature_tenths_c = source->temperature_tenths_c,
                .temperature_available = source->temperature_available,
            };
        }
    }
    result.ordered_modules = input->ordered_modules;
    memcpy(result.module_order, input->module_order, sizeof(result.module_order));
    if (!wind_renderer_dashboard_valid(&result)) return -2;
    *dashboard = result;
    return 0;
}

int wind_renderer_input_v2_render(const wind_renderer_input_v2_t *input,
                                  uint8_t *palette_out, size_t palette_size,
                                  wind_renderer_stats_t *stats) {
    wind_renderer_dashboard_t dashboard;
    if (wind_renderer_input_v2_to_dashboard(input, &dashboard) != 0) return -1;
    return wind_renderer_render(&dashboard, palette_out, palette_size, stats);
}

int wind_renderer_input_v2_render_preview_rgba(const wind_renderer_input_v2_t *input,
                                               uint8_t *rgba_out, size_t rgba_size,
                                               wind_renderer_stats_t *stats) {
    wind_renderer_dashboard_t dashboard;
    if (wind_renderer_input_v2_to_dashboard(input, &dashboard) != 0) return -1;
    return wind_renderer_render_preview_rgba(&dashboard, rgba_out, rgba_size, stats);
}

int wind_renderer_input_v2_render_preview_rgba_for_display(
    const wind_renderer_input_v2_t *input, wind_renderer_display_t display,
    uint8_t *rgba_out, size_t rgba_size, wind_renderer_stats_t *stats) {
    wind_renderer_dashboard_t dashboard;
    if (wind_renderer_input_v2_to_dashboard(input, &dashboard) != 0) return -1;
    return wind_renderer_render_preview_rgba_for_display(&dashboard, display, rgba_out,
                                                         rgba_size, stats);
}
