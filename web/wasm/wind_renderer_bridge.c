#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <emscripten/emscripten.h>

#include "wind_renderer.h"

enum {
    METADATA_SPOT_NAME = 0,
    METADATA_PROVIDER,
    METADATA_UPDATED_TIME,
    DAY_LABEL_DAY = 0,
    DAY_LABEL_DATE,
};

static wind_renderer_input_v2_t renderer_input;
static uint8_t renderer_output[WIND_RENDERER_PALETTE_BYTES];
static uint8_t renderer_preview_output[WIND_RENDERER_E1003_RGBA_BYTES];
static char string_scratch[WIND_RENDERER_SPOT_NAME_CAPACITY];
static int input_ready;
static int input_error;
static int output_valid;
static int preview_output_valid;
static uint32_t preview_width;
static uint32_t preview_height;

static int copy_text(char *destination, size_t capacity) {
    if (!destination || capacity == 0 ||
        memchr(string_scratch, '\0', sizeof(string_scratch)) == NULL) {
        return -1;
    }
    const size_t length = strlen(string_scratch);
    if (length >= capacity) return -1;
    memcpy(destination, string_scratch, length + 1);
    return 0;
}

static int accept_result(int result) {
    if (!input_ready || input_error) return -1;
    if (result != 0) {
        input_error = 1;
        output_valid = 0;
        preview_output_valid = 0;
        return result;
    }
    output_valid = 0;
    preview_output_valid = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_contract_version(void) {
    return wind_renderer_contract_version();
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_width(void) {
    return WIND_RENDERER_WIDTH;
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_height(void) {
    return WIND_RENDERER_HEIGHT;
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_palette_bytes(void) {
    return WIND_RENDERER_PALETTE_BYTES;
}

EMSCRIPTEN_KEEPALIVE uintptr_t wind_wasm_scratch_ptr(void) {
    return (uintptr_t)string_scratch;
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_scratch_capacity(void) {
    return sizeof(string_scratch);
}

EMSCRIPTEN_KEEPALIVE uintptr_t wind_wasm_output_ptr(void) {
    return output_valid ? (uintptr_t)renderer_output : 0;
}

EMSCRIPTEN_KEEPALIVE uintptr_t wind_wasm_preview_output_ptr(void) {
    return preview_output_valid ? (uintptr_t)renderer_preview_output : 0;
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_preview_bytes(void) {
    return preview_width * preview_height * 4u;
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_preview_width(void) {
    return preview_width;
}

EMSCRIPTEN_KEEPALIVE uint32_t wind_wasm_preview_height(void) {
    return preview_height;
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_reset(uint32_t contract_version) {
    memset(string_scratch, 0, sizeof(string_scratch));
    input_ready = 0;
    input_error = 0;
    output_valid = 0;
    preview_output_valid = 0;
    preview_width = 0;
    preview_height = 0;
    if (contract_version != WIND_RENDERER_CONTRACT_VERSION) return -1;
    wind_renderer_input_v2_init(&renderer_input);
    input_ready = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_metadata_field(int field) {
    if (!input_ready || input_error) return -1;
    int result = -1;
    switch (field) {
        case METADATA_SPOT_NAME:
            result =
                copy_text(renderer_input.spot_name, sizeof(renderer_input.spot_name));
            break;
        case METADATA_PROVIDER:
            result =
                copy_text(renderer_input.provider, sizeof(renderer_input.provider));
            break;
        case METADATA_UPDATED_TIME:
            result = copy_text(renderer_input.updated_time,
                               sizeof(renderer_input.updated_time));
            break;
        default:
            break;
    }
    return accept_result(result);
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_status(int state, int refresh_failed,
                                              int age_hours, int battery_percent,
                                              int display_mode, int threshold_kt) {
    if (!input_ready || input_error) return -1;
    return accept_result(wind_renderer_input_v2_set_status(
        &renderer_input, (wind_renderer_state_t)state, refresh_failed, age_hours,
        battery_percent, (wind_renderer_display_mode_t)display_mode, threshold_kt));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_display_rows(int show_weather,
                                                    int show_temperature, int show_tide,
                                                    int tide_available) {
    if (!input_ready || input_error) return -1;
    return accept_result(wind_renderer_input_v2_set_display_rows(
        &renderer_input, show_weather, show_temperature, show_tide, tide_available));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_preferences(int use_24_hour,
                                                   int temperature_fahrenheit,
                                                   int show_dedicated_footer) {
    if (!input_ready || input_error) return -1;
    return accept_result(wind_renderer_input_v2_set_preferences(
        &renderer_input, use_24_hour, temperature_fahrenheit, show_dedicated_footer));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_day_field(int day_index, int field) {
    if (!input_ready || input_error || day_index < 0 ||
        day_index >= WIND_RENDERER_DAY_COUNT) {
        input_error = 1;
        return -1;
    }
    int result = -1;
    if (field == DAY_LABEL_DAY) {
        result = copy_text(renderer_input.days[day_index].day,
                           sizeof(renderer_input.days[day_index].day));
    } else if (field == DAY_LABEL_DATE) {
        result = copy_text(renderer_input.days[day_index].date,
                           sizeof(renderer_input.days[day_index].date));
    }
    return accept_result(result);
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_sample_label(int day_index, int sample_index) {
    if (!input_ready || input_error || day_index < 0 ||
        day_index >= WIND_RENDERER_DAY_COUNT || sample_index < 0 ||
        sample_index >= WIND_RENDERER_SAMPLES_PER_DAY) {
        input_error = 1;
        return -1;
    }
    return accept_result(
        copy_text(renderer_input.days[day_index].samples[sample_index].time,
                  sizeof(renderer_input.days[day_index].samples[sample_index].time)));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_sample_values(int day_index, int sample_index,
                                                     int sustained_kt, int gust_kt,
                                                     int destination_degrees,
                                                     int available, int weather,
                                                     int temperature_tenths_c,
                                                     int temperature_available) {
    if (!input_ready || input_error) return -1;
    const char *time = NULL;
    if (day_index >= 0 && day_index < WIND_RENDERER_DAY_COUNT && sample_index >= 0 &&
        sample_index < WIND_RENDERER_SAMPLES_PER_DAY) {
        time = renderer_input.days[day_index].samples[sample_index].time;
    }
    return accept_result(wind_renderer_input_v2_set_sample(
        &renderer_input, day_index, sample_index, time, sustained_kt, gust_kt,
        destination_degrees, available, (wind_renderer_weather_t)weather,
        temperature_tenths_c, temperature_available));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_swell_sample(int day, int sample,
    int height_cm, int period_tenths, int destination_degrees) {
    if (!input_ready || input_error) return -1;
    return accept_result(wind_renderer_input_v2_set_swell(&renderer_input,
        day, sample, height_cm, period_tenths, destination_degrees));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_modules(int wind_size, int swell_size) {
    if (!input_ready || input_error) return -1;
    if (wind_size < 0 || wind_size > 2 || swell_size < 0 || swell_size > 2) return accept_result(-1);
    renderer_input.custom_modules = 1;
    renderer_input.wind_size = wind_size;
    renderer_input.swell_size = swell_size;
    return accept_result(0);
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_swell_hour(int day, int hour, int height_cm) {
    if (!input_ready || input_error) return -1;
    if (day < 0 || day >= WIND_RENDERER_DAY_COUNT || hour < 0 || hour > 23 ||
        height_cm < -1 || height_cm > 10000) return accept_result(-1);
    renderer_input.swell_hourly[day][hour] = height_cm;
    return accept_result(0);
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_secondary_swell_hour(int day, int hour, int height_cm) {
    if (!input_ready || input_error) return -1;
    if (day < 0 || day >= WIND_RENDERER_DAY_COUNT || hour < 0 || hour > 23 ||
        height_cm < -1 || height_cm > 10000) return accept_result(-1);
    renderer_input.secondary_swell_hourly[day][hour] = height_cm;
    return accept_result(0);
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_secondary_swell_sample(int day, int sample,
    int height_cm, int period_tenths, int destination_degrees) {
    if (!input_ready || input_error) return -1;
    if (day < 0 || day >= WIND_RENDERER_DAY_COUNT || sample < 0 || sample >= WIND_RENDERER_SAMPLES_PER_DAY ||
        height_cm < -1 || height_cm > 10000 || period_tenths < -1 || period_tenths > 1000 ||
        destination_degrees < -1 || destination_degrees >= 360) return accept_result(-1);
    renderer_input.secondary_swell[day][sample] = (wind_renderer_swell_sample_t){height_cm, period_tenths, destination_degrees};
    return accept_result(0);
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_tide_sample(int tide_index, int day_index,
                                                   int local_hour, int sea_level_mm,
                                                   int available) {
    if (!input_ready || input_error) return -1;
    return accept_result(wind_renderer_input_v2_set_tide_sample(
        &renderer_input, tide_index, day_index, local_hour, sea_level_mm, available));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_tide_extremum(int extremum_index, int day_index,
                                                     int local_hour, int local_minute,
                                                     int sea_level_mm, int is_high,
                                                     int available) {
    if (!input_ready || input_error) return -1;
    return accept_result(wind_renderer_input_v2_set_tide_extremum(
        &renderer_input, extremum_index, day_index, local_hour, local_minute,
        sea_level_mm, is_high, available));
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_render(void) {
    output_valid = 0;
    if (!input_ready || input_error) return -1;
    wind_renderer_stats_t stats;
    const int result = wind_renderer_input_v2_render(&renderer_input, renderer_output,
                                                     sizeof(renderer_output), &stats);
    if (result != 0 || stats.dither_passes != 1 || stats.clipped_primitives != 0) {
        memset(renderer_output, 0, sizeof(renderer_output));
        return result != 0 ? result : -2;
    }
    output_valid = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_render_preview(int display) {
    preview_output_valid = 0;
    if (!input_ready || input_error) return -1;
    if (display == WIND_RENDERER_DISPLAY_E1003_GC16) {
        preview_width = WIND_RENDERER_WIDTH;
        preview_height = WIND_RENDERER_E1003_COMPOSITION_HEIGHT;
    } else if (display == WIND_RENDERER_DISPLAY_E1001_GRAY4 ||
               display == WIND_RENDERER_DISPLAY_E1002_SPECTRA6) {
        preview_width = WIND_RENDERER_WIDTH;
        preview_height = WIND_RENDERER_HEIGHT;
    } else {
        return -1;
    }
    wind_renderer_stats_t stats;
    const int result = wind_renderer_input_v2_render_preview_rgba_for_display(
        &renderer_input, (wind_renderer_display_t)display, renderer_preview_output,
        sizeof(renderer_preview_output), &stats);
    if (result != 0 || stats.dither_passes != 0 || stats.clipped_primitives != 0) {
        memset(renderer_preview_output, 0, sizeof(renderer_preview_output));
        return result != 0 ? result : -2;
    }
    preview_output_valid = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int wind_wasm_set_module_order(int a, int b, int c, int d, int e) {
    if (!input_ready || input_error) return -1;
    const int order[] = {a,b,c,d,e};
    unsigned seen = 0;
    for (int i = 0; i < 5; ++i) {
        if (order[i] < 0 || order[i] >= 5 || (seen & (1u << order[i]))) return accept_result(-1);
        seen |= 1u << order[i];
    }
    renderer_input.ordered_modules = 1;
    memcpy(renderer_input.module_order, order, sizeof(order));
    return accept_result(0);
}
