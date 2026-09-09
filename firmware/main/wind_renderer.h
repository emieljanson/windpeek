#ifndef WIND_RENDERER_H
#define WIND_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    WIND_RENDERER_WIDTH = 800,
    WIND_RENDERER_HEIGHT = 480,
    WIND_RENDERER_E1003_COMPOSITION_HEIGHT = 600,
    WIND_RENDERER_E1003_WIDTH = 1872,
    WIND_RENDERER_E1003_HEIGHT = 1404,
    WIND_RENDERER_DAY_COUNT = 5,
    WIND_RENDERER_SAMPLES_PER_DAY = 5,
    WIND_RENDERER_PALETTE_BYTES = WIND_RENDERER_WIDTH * WIND_RENDERER_HEIGHT,
    WIND_RENDERER_E1003_COMPOSITION_BYTES =
        WIND_RENDERER_WIDTH * WIND_RENDERER_E1003_COMPOSITION_HEIGHT,
    WIND_RENDERER_RGBA_BYTES = WIND_RENDERER_PALETTE_BYTES * 4,
    WIND_RENDERER_E1003_RGBA_BYTES = WIND_RENDERER_E1003_COMPOSITION_BYTES * 4,
    WIND_RENDERER_CONTRACT_VERSION = 10,
    WIND_RENDERER_MAX_TIDE_SAMPLES = 121,
    WIND_RENDERER_MAX_TIDE_EXTREMA = 32,
    WIND_RENDERER_MIN_THRESHOLD_KT = 5,
    WIND_RENDERER_DEFAULT_THRESHOLD_KT = 17,
    WIND_RENDERER_MAX_THRESHOLD_KT = 35,
    WIND_RENDERER_SPOT_NAME_CAPACITY = 96,
    WIND_RENDERER_PROVIDER_CAPACITY = 32,
    WIND_RENDERER_UPDATED_TIME_CAPACITY = 32,
    WIND_RENDERER_DAY_LABEL_CAPACITY = 16,
    WIND_RENDERER_DATE_LABEL_CAPACITY = 16,
    WIND_RENDERER_TIME_LABEL_CAPACITY = 8,
};

typedef enum {
    WIND_RENDERER_FRESH = 0,
    WIND_RENDERER_AGED,
    WIND_RENDERER_STALE,
    WIND_RENDERER_UNAVAILABLE,
} wind_renderer_state_t;

typedef enum {
    WIND_RENDERER_MODE_THRESHOLD = 1,
    WIND_RENDERER_MODE_SOLID = 2,
    WIND_RENDERER_MODE_COUNT = 3,
} wind_renderer_display_mode_t;

typedef enum {
    WIND_RENDERER_DISPLAY_E1001_GRAY4 = 1,
    WIND_RENDERER_DISPLAY_E1002_SPECTRA6 = 2,
    WIND_RENDERER_DISPLAY_E1003_GC16 = 3,
} wind_renderer_display_t;

typedef enum {
    WIND_RENDERER_WEATHER_UNAVAILABLE = 0,
    WIND_RENDERER_WEATHER_CLEAR_DAY,
    WIND_RENDERER_WEATHER_CLEAR_NIGHT,
    WIND_RENDERER_WEATHER_PARTLY_CLOUDY_DAY,
    WIND_RENDERER_WEATHER_PARTLY_CLOUDY_NIGHT,
    WIND_RENDERER_WEATHER_CLOUDY,
    WIND_RENDERER_WEATHER_LIGHT_RAIN,
    WIND_RENDERER_WEATHER_RAIN,
    WIND_RENDERER_WEATHER_HEAVY_RAIN,
} wind_renderer_weather_t;

typedef struct {
    int32_t height_cm;
    int32_t period_tenths;
    int32_t destination_degrees;
} wind_renderer_swell_sample_t;

typedef struct {
    const char *time;
    int sustained_kt;
    int gust_kt;
    int destination_degrees;
    int available;
    wind_renderer_weather_t weather;
    int temperature_tenths_c;
    int temperature_available;
} wind_renderer_sample_t;

typedef struct {
    int day_index;
    int local_hour;
    int sea_level_mm;
    int available;
} wind_renderer_tide_sample_t;

typedef struct {
    int day_index;
    int local_hour;
    int local_minute;
    int sea_level_mm;
    int is_high;
    int available;
} wind_renderer_tide_extremum_t;

typedef struct {
    const char *day;
    const char *date;
    wind_renderer_sample_t samples[WIND_RENDERER_SAMPLES_PER_DAY];
} wind_renderer_day_t;

typedef struct {
    const char *spot_name;
    const char *provider;
    const char *updated_time;
    wind_renderer_state_t state;
    int refresh_failed;
    int age_hours;
    /* Negative means that battery state is unknown. */
    int battery_percent;
    wind_renderer_display_mode_t display_mode;
    int threshold_kt;
    int custom_modules;
    int wind_size;
    int swell_size;
    int ordered_modules;
    int module_order[5];
    int swell_hourly[WIND_RENDERER_DAY_COUNT][24];
    int secondary_swell_hourly[WIND_RENDERER_DAY_COUNT][24];
    wind_renderer_swell_sample_t secondary_swell[WIND_RENDERER_DAY_COUNT][WIND_RENDERER_SAMPLES_PER_DAY];
    wind_renderer_swell_sample_t swell[WIND_RENDERER_DAY_COUNT][WIND_RENDERER_SAMPLES_PER_DAY];
    int show_weather;
    int show_temperature;
    int show_tide;
    int show_dedicated_footer;
    int use_24_hour;
    int temperature_fahrenheit;
    int tide_available;
    int tide_sample_count;
    wind_renderer_tide_sample_t tide_samples[WIND_RENDERER_MAX_TIDE_SAMPLES];
    int tide_extremum_count;
    wind_renderer_tide_extremum_t tide_extrema[WIND_RENDERER_MAX_TIDE_EXTREMA];
    wind_renderer_day_t days[WIND_RENDERER_DAY_COUNT];
} wind_renderer_dashboard_t;

/*
 * Versioned, bounded storage used by cross-runtime bridges. Its native byte
 * layout is intentionally not an ABI: browser callers must use bridge setter
 * functions rather than writing this structure from JavaScript.
 */
typedef struct {
    char time[WIND_RENDERER_TIME_LABEL_CAPACITY];
    int32_t sustained_kt;
    int32_t gust_kt;
    int32_t destination_degrees;
    int32_t available;
    int32_t weather;
    int32_t temperature_tenths_c;
    int32_t temperature_available;
} wind_renderer_input_sample_v2_t;

typedef struct {
    char day[WIND_RENDERER_DAY_LABEL_CAPACITY];
    char date[WIND_RENDERER_DATE_LABEL_CAPACITY];
    wind_renderer_input_sample_v2_t samples[WIND_RENDERER_SAMPLES_PER_DAY];
} wind_renderer_input_day_v2_t;

typedef struct {
    int32_t day_index;
    int32_t local_hour;
    int32_t sea_level_mm;
    int32_t available;
} wind_renderer_input_tide_sample_v2_t;

typedef struct {
    int32_t day_index;
    int32_t local_hour;
    int32_t local_minute;
    int32_t sea_level_mm;
    int32_t is_high;
    int32_t available;
} wind_renderer_input_tide_extremum_v2_t;

typedef struct {
    uint32_t version;
    char spot_name[WIND_RENDERER_SPOT_NAME_CAPACITY];
    char provider[WIND_RENDERER_PROVIDER_CAPACITY];
    char updated_time[WIND_RENDERER_UPDATED_TIME_CAPACITY];
    int32_t state;
    int32_t refresh_failed;
    int32_t age_hours;
    int32_t battery_percent;
    int32_t display_mode;
    int32_t threshold_kt;
    int32_t custom_modules;
    int32_t wind_size;
    int32_t swell_size;
    int ordered_modules;
    int module_order[5];
    int swell_hourly[WIND_RENDERER_DAY_COUNT][24];
    int secondary_swell_hourly[WIND_RENDERER_DAY_COUNT][24];
    wind_renderer_swell_sample_t secondary_swell[WIND_RENDERER_DAY_COUNT][WIND_RENDERER_SAMPLES_PER_DAY];
    wind_renderer_swell_sample_t swell[WIND_RENDERER_DAY_COUNT][WIND_RENDERER_SAMPLES_PER_DAY];
    int32_t show_weather;
    int32_t show_temperature;
    int32_t show_tide;
    int32_t show_dedicated_footer;
    int32_t use_24_hour;
    int32_t temperature_fahrenheit;
    int32_t tide_available;
    int32_t tide_sample_count;
    wind_renderer_input_tide_sample_v2_t tide_samples[WIND_RENDERER_MAX_TIDE_SAMPLES];
    int32_t tide_extremum_count;
    wind_renderer_input_tide_extremum_v2_t tide_extrema[WIND_RENDERER_MAX_TIDE_EXTREMA];
    wind_renderer_input_day_v2_t days[WIND_RENDERER_DAY_COUNT];
} wind_renderer_input_v2_t;

typedef struct {
    int dither_passes;
    int status_right;
    int clipped_primitives;
    int wind_baseline;
    int weather_row_top;
    int temperature_row_top;
    int tide_row_top;
} wind_renderer_stats_t;

/*
 * Composes and dithers one complete 800 x 480 dashboard. The caller owns
 * palette_out. Output bytes use the native Spectra palette indices for black,
 * white, and red.
 */
int wind_renderer_render(const wind_renderer_dashboard_t *dashboard,
                         uint8_t *palette_out, size_t palette_size,
                         wind_renderer_stats_t *stats);

/* Render the same composition with model-specific final pixel semantics. */
int wind_renderer_render_for_display(const wind_renderer_dashboard_t *dashboard,
                                     wind_renderer_display_t display,
                                     uint8_t *logical_out, size_t logical_size,
                                     wind_renderer_stats_t *stats);

/* Returns the physical panel size associated with a renderer model. */
int wind_renderer_display_dimensions(wind_renderer_display_t display, int *width,
                                     int *height);

/*
 * Projects one logical composition row onto its physical panel. E1003 scales
 * the 800 x 600 composition exactly to the native 1872 x 1404 pixels.
 */
int wind_renderer_project_display_row(wind_renderer_display_t display,
                                      const uint8_t *logical, size_t logical_size,
                                      int target_y, uint8_t *target_row,
                                      size_t target_row_size);

/* Mixes the display identity into panel-cache invalidation. */
uint64_t wind_renderer_display_signature(uint64_t base,
                                         wind_renderer_display_t display);

/*
 * Renders the same dashboard composition as wind_renderer_render, but keeps
 * continuous grayscale values and red accents for a clean browser preview.
 * Only the final output pass differs; layout and drawing stay shared.
 */
int wind_renderer_render_preview_rgba(const wind_renderer_dashboard_t *dashboard,
                                      uint8_t *rgba_out, size_t rgba_size,
                                      wind_renderer_stats_t *stats);

/* Browser preview at the model's native composition aspect and color depth. */
int wind_renderer_render_preview_rgba_for_display(
    const wind_renderer_dashboard_t *dashboard, wind_renderer_display_t display,
    uint8_t *rgba_out, size_t rgba_size, wind_renderer_stats_t *stats);

int wind_renderer_input_v2_render_preview_rgba_for_display(
    const wind_renderer_input_v2_t *input, wind_renderer_display_t display,
    uint8_t *rgba_out, size_t rgba_size, wind_renderer_stats_t *stats);

uint32_t wind_renderer_contract_version(void);

void wind_renderer_input_v2_init(wind_renderer_input_v2_t *input);

int wind_renderer_input_v2_set_metadata(wind_renderer_input_v2_t *input,
                                        const char *spot_name, const char *provider,
                                        const char *updated_time);

int wind_renderer_input_v2_set_status(wind_renderer_input_v2_t *input,
                                      wind_renderer_state_t state, int refresh_failed,
                                      int age_hours, int battery_percent,
                                      wind_renderer_display_mode_t display_mode,
                                      int threshold_kt);

int wind_renderer_input_v2_set_display_rows(wind_renderer_input_v2_t *input,
                                            int show_weather, int show_temperature,
                                            int show_tide, int tide_available);

int wind_renderer_input_v2_set_swell(wind_renderer_input_v2_t *input,
    int day_index, int sample_index, int height_cm, int period_tenths, int destination_degrees);

int wind_renderer_input_v2_set_preferences(wind_renderer_input_v2_t *input,
                                           int use_24_hour, int temperature_fahrenheit,
                                           int show_dedicated_footer);

int wind_renderer_input_v2_set_day(wind_renderer_input_v2_t *input, int day_index,
                                   const char *day, const char *date);

int wind_renderer_input_v2_set_sample(wind_renderer_input_v2_t *input, int day_index,
                                      int sample_index, const char *time,
                                      int sustained_kt, int gust_kt,
                                      int destination_degrees, int available,
                                      wind_renderer_weather_t weather,
                                      int temperature_tenths_c,
                                      int temperature_available);

int wind_renderer_input_v2_set_tide_sample(wind_renderer_input_v2_t *input,
                                           int tide_index, int day_index,
                                           int local_hour, int sea_level_mm,
                                           int available);

int wind_renderer_input_v2_set_tide_extremum(wind_renderer_input_v2_t *input,
                                             int extremum_index, int day_index,
                                             int local_hour, int local_minute,
                                             int sea_level_mm, int is_high,
                                             int available);

/*
 * Creates the canonical pointer-based dashboard view over input. The returned
 * dashboard remains valid only while input remains alive and unchanged.
 */
int wind_renderer_input_v2_to_dashboard(const wind_renderer_input_v2_t *input,
                                        wind_renderer_dashboard_t *dashboard);

int wind_renderer_input_v2_render(const wind_renderer_input_v2_t *input,
                                  uint8_t *palette_out, size_t palette_size,
                                  wind_renderer_stats_t *stats);

int wind_renderer_input_v2_render_preview_rgba(const wind_renderer_input_v2_t *input,
                                               uint8_t *rgba_out, size_t rgba_size,
                                               wind_renderer_stats_t *stats);

/*
 * Expands one renderer palette row to RGB888 for display-manager streaming.
 * Expands native black, white, and red palette values to RGB888.
 */
int wind_renderer_palette_row_to_rgb(const uint8_t *palette_row, size_t width,
                                     uint8_t *rgb_row, size_t rgb_size);

#ifdef __cplusplus
}
#endif

#endif
