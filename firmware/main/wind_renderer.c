#include "wind_renderer.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootstrap_weather_icons.h"
#include "wind_font.h"

enum {
    CANVAS_WHITE = 255,
    CANVAS_BLACK = 0,
    PALETTE_BLACK = 0,
    PALETTE_WHITE = 1,
    PALETTE_RED = 3,
    OUTER_X = 12,
    OUTER_TOP = 12,
    OUTER_RIGHT = 787,
    OUTER_BOTTOM = 467,
    CONTENT_LEFT = 30,
    CONTENT_RIGHT = 770,
    HEADER_BOTTOM = 80,
    DAY_HEADER_BOTTOM = 113,
    FOOTER_TOP = 449,
    FOOTER_TIME_DAY_COUNT = 3,
    FOOTER_STATUS_GAP = 8,
    FOOTER_TEXT_OFFSET_Y = 2,
    GRAPH_TOP = 155,
    BAR_GRAPH_TOP = 159,
    HEADER_TEXT_BASELINE = 62,
    DAY_LABEL_BASELINE = 102,
    DIRECTION_CENTER_Y = 133,
    SUSTAINED_BAR_WIDTH = 16,
    WEATHER_ROW_HEIGHT = 35,
    TEMPERATURE_ROW_HEIGHT = 35,
    COMBINED_CONDITIONS_ROW_HEIGHT = 54,
    TIDE_ROW_HEIGHT = 60,
    MODULE_PADDING = 8,
    MODULE_CELL = 20,
    MODULE_TEXT_CELL = 16,
    MODULE_GAP = 8,
    MODULE_GRAPH_INSET = MODULE_PADDING * 2 + MODULE_CELL,
    MODULE_PERIOD_INSET = MODULE_PADDING * 2 + MODULE_TEXT_CELL,
    MODULE_LABEL_HEIGHT = 12,
    MODULE_COMPACT_HEIGHT = MODULE_PADDING * 2 + MODULE_CELL + MODULE_TEXT_CELL * 2 + MODULE_GAP * 2,
    MODULE_TIDE_LABEL_GAP = 2,
    MODULE_TIDE_CURVE_HEIGHT = 16,
    MODULE_TIDE_HEIGHT = MODULE_PADDING * 2 + MODULE_TEXT_CELL * 2 + MODULE_TIDE_LABEL_GAP * 2 + MODULE_TIDE_CURVE_HEIGHT,
    SWELL_SCALE_CM = 1000,
    SWELL_LINE_WIDTH = 1,
    FORECAST_FIRST_HOUR = 8,
    FORECAST_LAST_HOUR = 20,
    TIDE_DATA_FIRST_HOUR = 5,
    TIDE_DATA_LAST_HOUR = 23,
};

typedef struct {
    int wind_baseline;
    int modules_bottom;
    int direction_center;
    int chart_scale_height;
    bool inset_labels;
    int weather_top;
    int weather_bottom;
    int weather_center;
    int temperature_top;
    int temperature_bottom;
    int tide_top;
    int tide_bottom;
    bool combined_conditions;
} dashboard_layout_t;

typedef struct {
    uint8_t *pixels;
    int height;
    size_t size;
    int clipped;
    bool antialias_text;
    bool smooth_curves;
} canvas_t;

typedef enum {
    OUTPUT_PALETTE,
    OUTPUT_GRAY4,
    OUTPUT_GC16,
    OUTPUT_RGBA,
    OUTPUT_RGBA_GC16,
} output_format_t;

typedef enum {
    OUTPUT_BLACK,
    OUTPUT_WHITE,
    OUTPUT_RED,
} output_color_t;

typedef struct {
    uint8_t *pixels;
    output_format_t format;
} output_surface_t;

static const char *safe_text(const char *text) {
    return text ? text : "";
}

static void uppercase_spot_name(char *output, size_t output_size, const char *input) {
    const unsigned char *source = (const unsigned char *)safe_text(input);
    size_t used = 0;
    if (!output || output_size == 0) return;
    while (*source && used + 1 < output_size) {
        if (*source >= 'a' && *source <= 'z') {
            output[used++] = (char)(*source - ('a' - 'A'));
            ++source;
            continue;
        }
        if ((source[0] == 0xc4 || source[0] == 0xc5) &&
            (source[1] & 0xc0) == 0x80 && used + 2 < output_size) {
            unsigned int cp = ((source[0] & 0x1f) << 6) | (source[1] & 0x3f);
            if (cp == 0x0131 || cp == 0x0138 || cp == 0x017f) {
                output[used++] = cp == 0x0131 ? 'I' : cp == 0x0138 ? 'K' : 'S';
                source += 2;
                continue;
            }
            /* Latin Extended-A pairs change parity at U+0139 and U+014A. */
            if ((cp <= 0x012f && (cp & 1)) ||
                (cp >= 0x0133 && cp <= 0x0137 && (cp & 1)) ||
                (cp >= 0x013a && cp <= 0x0148 && !(cp & 1)) ||
                (cp >= 0x014b && cp <= 0x0177 && (cp & 1)) ||
                (cp >= 0x017a && cp <= 0x017e && !(cp & 1)))
                --cp;
            output[used++] = (char)(0xc0 | (cp >> 6));
            output[used++] = (char)(0x80 | (cp & 0x3f));
            source += 2;
            continue;
        }
        if (source[0] == 0xc3 && source[1] && used + 2 < output_size) {
            unsigned char second = source[1];
            if (second == 0xbf) { /* U+00FF uppercases to U+0178. */
                output[used++] = (char)0xc5;
                output[used++] = (char)0xb8;
                source += 2;
                continue;
            }
            if (second >= 0xa0 && second <= 0xbe && second != 0xb7)
                second = (unsigned char)(second - 0x20);
            output[used++] = (char)source[0];
            output[used++] = (char)second;
            source += 2;
            continue;
        }
        output[used++] = (char)*source++;
    }
    output[used] = '\0';
}

static int divide_rounded(int value, int divisor);
static void draw_battery(canvas_t *canvas, int right, int center_y, int percent);

static int canvas_outer_bottom(const canvas_t *canvas) {
    return OUTER_BOTTOM + canvas->height - WIND_RENDERER_HEIGHT;
}

static int canvas_footer_top(const canvas_t *canvas) {
    return FOOTER_TOP + canvas->height - WIND_RENDERER_HEIGHT;
}

/* Shared fixed-height slots for icons and numeric text. Text uses the
 * digit cap height rather than a font's descender space for optical centering. */
static int module_text_center(int top, int row, bool after_icon) {
    return top + MODULE_PADDING + (after_icon ? MODULE_CELL + MODULE_GAP : 0) +
        MODULE_TEXT_CELL / 2 + row * (MODULE_TEXT_CELL + MODULE_GAP);
}

static int module_cell_center(int top, int row) {
    return row == 0 ? top + MODULE_PADDING + MODULE_CELL / 2 :
        module_text_center(top, row - 1, true);
}

static int module_text_baseline(int center) {
    return center + 6;
}

static dashboard_layout_t dashboard_layout(const wind_renderer_dashboard_t *dashboard,
                                           const canvas_t *canvas) {
    dashboard_layout_t layout = {0};
    int cursor = dashboard->show_dedicated_footer ? canvas_footer_top(canvas)
                                                  : canvas_outer_bottom(canvas);
    const int tide_height = dashboard->custom_modules ? MODULE_TIDE_HEIGHT : TIDE_ROW_HEIGHT;
    if (dashboard->show_tide) {
        layout.tide_bottom = cursor - 1;
        cursor -= tide_height;
        layout.tide_top = cursor;
    }
    if (dashboard->show_weather && dashboard->show_temperature) {
        layout.combined_conditions = true;
        cursor -= COMBINED_CONDITIONS_ROW_HEIGHT;
        layout.weather_top = cursor;
        layout.weather_bottom = cursor + 27;
        layout.weather_center = cursor + 16;
        layout.temperature_top = cursor + 28;
        layout.temperature_bottom = cursor + COMBINED_CONDITIONS_ROW_HEIGHT - 1;
    } else if (dashboard->show_temperature) {
        layout.temperature_bottom = cursor - 1;
        cursor -= TEMPERATURE_ROW_HEIGHT;
        layout.temperature_top = cursor;
    } else if (dashboard->show_weather) {
        layout.weather_bottom = cursor - 1;
        cursor -= WEATHER_ROW_HEIGHT;
        layout.weather_top = cursor;
        layout.weather_center = cursor + 18;
    }
    if (dashboard->custom_modules) {
        cursor = dashboard->show_dedicated_footer ? canvas_footer_top(canvas) : canvas_outer_bottom(canvas);
        if (dashboard->show_tide) cursor -= tide_height;
        const int rows = dashboard->show_weather + dashboard->show_temperature;
        if (rows) {
            const int conditions_height = MODULE_PADDING * 2 + MODULE_CELL * dashboard->show_weather +
                MODULE_TEXT_CELL * dashboard->show_temperature + MODULE_GAP * (rows - 1);
            cursor -= conditions_height;
            if (dashboard->show_weather) {
                layout.weather_top = cursor;
                layout.weather_center = module_cell_center(cursor, 0);
            }
            if (dashboard->show_temperature) {
                layout.temperature_top = cursor;
                layout.temperature_bottom = cursor + conditions_height - 1;
            }
        }
    }
    layout.modules_bottom = cursor;
    layout.direction_center = DIRECTION_CENTER_Y;
    layout.wind_baseline = cursor - 8;
    layout.chart_scale_height = layout.wind_baseline - BAR_GRAPH_TOP;
    return layout;
}

static int clamp_int(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static int day_column_x(int day) {
    return OUTER_X + (OUTER_RIGHT - OUTER_X) * day / WIND_RENDERER_DAY_COUNT;
}

static int forecast_sample_center_x(int day, int sample) {
    const int column_width = day_column_x(day + 1) - day_column_x(day);
    const int sample_step = (column_width + (WIND_RENDERER_SAMPLES_PER_DAY + 1) / 2) /
                            (WIND_RENDERER_SAMPLES_PER_DAY + 1);
    return day_column_x(day) + (sample + 1) * sample_step;
}

static int tide_time_x(int day, int hour, int minute) {
    /* The forecast centers are the time axis: 08:00 and 20:00 therefore
     * share exactly the same pixels as the first and last wind samples. */
    const int first_x = forecast_sample_center_x(day, 0);
    const int last_x = forecast_sample_center_x(day, WIND_RENDERER_SAMPLES_PER_DAY - 1);
    return first_x + divide_rounded(((hour - FORECAST_FIRST_HOUR) * 60 + minute) *
                                        (last_x - first_x),
                                    (FORECAST_LAST_HOUR - FORECAST_FIRST_HOUR) * 60);
}

static int tide_hour_x(int day, int hour) {
    return tide_time_x(day, hour, 0);
}

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

static int dashboard_valid(const wind_renderer_dashboard_t *dashboard) {
    if (!dashboard || dashboard->state < WIND_RENDERER_FRESH ||
        dashboard->state > WIND_RENDERER_UNAVAILABLE ||
        (dashboard->refresh_failed != 0 && dashboard->refresh_failed != 1) ||
        (dashboard->custom_modules != 0 && dashboard->custom_modules != 1) ||
        (dashboard->custom_modules && (dashboard->wind_size < 0 || dashboard->wind_size > 2 ||
         dashboard->swell_size < 0 || dashboard->swell_size > 2)) ||
        dashboard->age_hours < 0 || dashboard->battery_percent < -1 ||
        dashboard->battery_percent > 100 ||
        (dashboard->display_mode != WIND_RENDERER_MODE_THRESHOLD &&
         dashboard->display_mode != WIND_RENDERER_MODE_SOLID) ||
        dashboard->threshold_kt < WIND_RENDERER_MIN_THRESHOLD_KT ||
        dashboard->threshold_kt > WIND_RENDERER_MAX_THRESHOLD_KT ||
        (dashboard->show_weather != 0 && dashboard->show_weather != 1) ||
        (dashboard->show_temperature != 0 && dashboard->show_temperature != 1) ||
        (dashboard->show_tide != 0 && dashboard->show_tide != 1) ||
        (dashboard->show_dedicated_footer != 0 &&
         dashboard->show_dedicated_footer != 1) ||
        (dashboard->use_24_hour != 0 && dashboard->use_24_hour != 1) ||
        (dashboard->temperature_fahrenheit != 0 &&
         dashboard->temperature_fahrenheit != 1) ||
        (dashboard->tide_available != 0 && dashboard->tide_available != 1) ||
        dashboard->tide_sample_count < 0 ||
        dashboard->tide_sample_count > WIND_RENDERER_MAX_TIDE_SAMPLES ||
        dashboard->tide_extremum_count < 0 ||
        dashboard->tide_extremum_count > WIND_RENDERER_MAX_TIDE_EXTREMA ||
        !text_fits(dashboard->spot_name, WIND_RENDERER_SPOT_NAME_CAPACITY) ||
        !text_fits(dashboard->provider, WIND_RENDERER_PROVIDER_CAPACITY) ||
        !text_fits(dashboard->updated_time, WIND_RENDERER_UPDATED_TIME_CAPACITY)) {
        return 0;
    }
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        if (!text_fits(dashboard->days[day].day, WIND_RENDERER_DAY_LABEL_CAPACITY) ||
            !text_fits(dashboard->days[day].date, WIND_RENDERER_DATE_LABEL_CAPACITY)) {
            return 0;
        }
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            const wind_renderer_sample_t *slot = &dashboard->days[day].samples[sample];
            const wind_renderer_swell_sample_t *swell = &dashboard->swell[day][sample];
            if (dashboard->custom_modules && (swell->height_cm < -1 || swell->height_cm > 10000 ||
                swell->period_tenths < -1 || swell->period_tenths > 1000 ||
                swell->destination_degrees < -1 || swell->destination_degrees >= 360)) return 0;
            if ((slot->available != 0 && slot->available != 1) ||
                slot->weather < WIND_RENDERER_WEATHER_UNAVAILABLE ||
                slot->weather > WIND_RENDERER_WEATHER_HEAVY_RAIN ||
                (slot->temperature_available != 0 &&
                 slot->temperature_available != 1) ||
                !text_fits(slot->time, WIND_RENDERER_TIME_LABEL_CAPACITY) ||
                (slot->available && (!slot->time || !slot->time[0]))) {
                return 0;
            }
        }
    }
    for (int index = 0; index < dashboard->tide_sample_count; ++index) {
        const wind_renderer_tide_sample_t *sample = &dashboard->tide_samples[index];
        if ((sample->available != 0 && sample->available != 1) ||
            sample->day_index < 0 || sample->day_index >= WIND_RENDERER_DAY_COUNT ||
            sample->local_hour < 0 || sample->local_hour > 23)
            return 0;
    }
    for (int index = 0; index < dashboard->tide_extremum_count; ++index) {
        const wind_renderer_tide_extremum_t *extremum = &dashboard->tide_extrema[index];
        if ((extremum->available != 0 && extremum->available != 1) ||
            (extremum->is_high != 0 && extremum->is_high != 1) ||
            extremum->day_index < 0 || extremum->day_index >= WIND_RENDERER_DAY_COUNT ||
            extremum->local_hour < 0 || extremum->local_hour > 23 ||
            extremum->local_minute < 0 || extremum->local_minute > 59 ||
            extremum->local_minute % 15 != 0)
            return 0;
    }
    if (dashboard->tide_available && dashboard->tide_sample_count < 2) return 0;
    return 1;
}

static void set_pixel(canvas_t *canvas, int x, int y, uint8_t gray) {
    if (x < 0 || x >= WIND_RENDERER_WIDTH || y < 0 || y >= canvas->height) {
        canvas->clipped++;
        return;
    }
    canvas->pixels[y * WIND_RENDERER_WIDTH + x] = gray;
}

static void fill_rect(canvas_t *canvas, int x, int y, int width, int height,
                      uint8_t gray) {
    if (width <= 0 || height <= 0) return;
    if (x < 0 || y < 0 || x + width > WIND_RENDERER_WIDTH ||
        y + height > canvas->height) {
        canvas->clipped++;
        return;
    }
    for (int row = y; row < y + height; ++row)
        memset(canvas->pixels + row * WIND_RENDERER_WIDTH + x, gray, (size_t)width);
}

static void horizontal_line(canvas_t *canvas, int x0, int x1, int y, uint8_t gray) {
    if (x1 < x0) {
        const int swap = x0;
        x0 = x1;
        x1 = swap;
    }
    fill_rect(canvas, x0, y, x1 - x0 + 1, 1, gray);
}

static void vertical_line(canvas_t *canvas, int x, int y0, int y1, uint8_t gray) {
    if (y1 < y0) {
        const int swap = y0;
        y0 = y1;
        y1 = swap;
    }
    for (int y = y0; y <= y1; ++y)
        set_pixel(canvas, x, y, gray);
}

static void outline_rect(canvas_t *canvas, int x, int y, int width, int height) {
    horizontal_line(canvas, x, x + width - 1, y, CANVAS_BLACK);
    horizontal_line(canvas, x, x + width - 1, y + height - 1, CANVAS_BLACK);
    vertical_line(canvas, x, y, y + height - 1, CANVAS_BLACK);
    vertical_line(canvas, x + width - 1, y, y + height - 1, CANVAS_BLACK);
}

static void draw_text_color(canvas_t *canvas, int x, int baseline,
                            wind_font_family_t family, int size, uint8_t gray,
                            const char *text);

static void draw_text(canvas_t *canvas, int x, int baseline, wind_font_family_t family,
                      int size, const char *text) {
    draw_text_color(canvas, x, baseline, family, size, CANVAS_BLACK, text);
}

static void draw_text_mask(canvas_t *canvas, int x, int baseline,
                           wind_font_family_t family, int size, const char *text) {
    wind_font_draw(canvas->pixels, WIND_RENDERER_WIDTH, canvas->height,
                   WIND_RENDERER_WIDTH, x, baseline, family, size, CANVAS_BLACK,
                   safe_text(text));
}

static void fade_region_to_white(canvas_t *canvas, int left, int top, int right,
                                 int bottom) {
    left = clamp_int(left, 0, WIND_RENDERER_WIDTH - 1);
    right = clamp_int(right, 0, WIND_RENDERER_WIDTH - 1);
    top = clamp_int(top, 0, canvas->height - 1);
    bottom = clamp_int(bottom, 0, canvas->height - 1);
    if (right <= left || bottom < top) return;

    const int span = right - left;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int linear = (x - left) * 255 / span;
            const int fade = (linear * linear * (765 - 2 * linear) + 32512) / 65025;
            uint8_t *pixel = &canvas->pixels[y * WIND_RENDERER_WIDTH + x];
            *pixel = (uint8_t)((int)*pixel +
                               ((CANVAS_WHITE - (int)*pixel) * fade + 127) / 255);
        }
    }
}

static int rightmost_ink_pixel(const canvas_t *canvas, int left, int top, int right,
                               int bottom) {
    left = clamp_int(left, 0, WIND_RENDERER_WIDTH - 1);
    right = clamp_int(right, 0, WIND_RENDERER_WIDTH - 1);
    top = clamp_int(top, 0, canvas->height - 1);
    bottom = clamp_int(bottom, 0, canvas->height - 1);
    for (int x = right; x >= left; --x) {
        for (int y = top; y <= bottom; ++y) {
            if (canvas->pixels[y * WIND_RENDERER_WIDTH + x] < CANVAS_WHITE) return x;
        }
    }
    return left;
}

static void draw_text_color(canvas_t *canvas, int x, int baseline,
                            wind_font_family_t family, int size, uint8_t gray,
                            const char *text) {
    if (canvas->antialias_text) {
        wind_font_draw_antialiased(canvas->pixels, WIND_RENDERER_WIDTH, canvas->height,
                                   WIND_RENDERER_WIDTH, x, baseline, family, size, gray,
                                   safe_text(text));
    } else {
        wind_font_draw(canvas->pixels, WIND_RENDERER_WIDTH, canvas->height,
                       WIND_RENDERER_WIDTH, x, baseline, family, size, gray,
                       safe_text(text));
    }
}

static void draw_outlined_text_center(canvas_t *canvas, int center_x, int baseline,
                                      wind_font_family_t family, int size,
                                      const char *text) {
    const wind_text_metrics_t metrics =
        wind_font_measure(family, size, safe_text(text));
    const int x = center_x - metrics.width / 2;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if ((dx == 0 && dy == 0) || dx * dx + dy * dy > 4) continue;
            draw_text_color(canvas, x + dx, baseline + dy, family, size, CANVAS_WHITE,
                            text);
        }
    }
    draw_text(canvas, x, baseline, family, size, text);
}

static void draw_text_right(canvas_t *canvas, int right, int baseline,
                            wind_font_family_t family, int size, const char *text) {
    const wind_text_metrics_t metrics =
        wind_font_measure(family, size, safe_text(text));
    draw_text(canvas, right - metrics.width + 1, baseline, family, size, text);
}

static int triangle_edge(int ax, int ay, int bx, int by, int px, int py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void fill_triangle(canvas_t *canvas, int ax, int ay, int bx, int by, int cx,
                          int cy) {
    const int min_x = clamp_int(ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx), 0,
                                WIND_RENDERER_WIDTH - 1);
    const int max_x = clamp_int(ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx), 0,
                                WIND_RENDERER_WIDTH - 1);
    const int min_y = clamp_int(ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy), 0,
                                canvas->height - 1);
    const int max_y = clamp_int(ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy), 0,
                                canvas->height - 1);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const int e0 = triangle_edge(ax, ay, bx, by, x, y);
            const int e1 = triangle_edge(bx, by, cx, cy, x, y);
            const int e2 = triangle_edge(cx, cy, ax, ay, x, y);
            if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))
                set_pixel(canvas, x, y, CANVAS_BLACK);
        }
    }
}

static void draw_arrow(canvas_t *canvas, int center_x, int center_y,
                       int destination_degrees) {
    /* Exact Figma navigation-arrow silhouette, node 371:2786.
       The four points are the tip, left tail, concave notch, and right tail.
       Shift its filled-area centroid onto the rotation origin so the asymmetric
       silhouette remains optically centered at every direction. */
    static const float source[4][2] = {
        {0.0f, -8.25f},
        {-6.5f, 5.75f},
        {0.0f, 2.45f},
        {6.5f, 5.75f},
    };
    int degrees = destination_degrees % 360;
    if (degrees < 0) degrees += 360;
    const float radians = (float)degrees * 0.01745329252f;
    const float sine = sinf(radians);
    const float cosine = cosf(radians);
    int points[4][2];
    for (int point = 0; point < 4; ++point) {
        points[point][0] = center_x + (int)lroundf(source[point][0] * cosine -
                                                   source[point][1] * sine);
        points[point][1] = center_y + (int)lroundf(source[point][0] * sine +
                                                   source[point][1] * cosine);
    }
    fill_triangle(canvas, points[0][0], points[0][1], points[1][0], points[1][1],
                  points[2][0], points[2][1]);
    fill_triangle(canvas, points[0][0], points[0][1], points[2][0], points[2][1],
                  points[3][0], points[3][1]);
}

static void draw_alpha_icon(canvas_t *canvas, int center_x, int center_y, int width,
                            int height, const uint8_t *alpha) {
    const int left = center_x - width / 2;
    const int top = center_y - height / 2;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t opacity = alpha[y * width + x];
            if (!opacity) continue;
            const int pixel_x = left + x;
            const int pixel_y = top + y;
            const uint8_t background =
                canvas->pixels[pixel_y * WIND_RENDERER_WIDTH + pixel_x];
            set_pixel(canvas, pixel_x, pixel_y,
                      (uint8_t)((background * (255 - opacity) + 127) / 255));
        }
    }
}

static void draw_weather(canvas_t *canvas, int center_x, int center_y,
                         wind_renderer_weather_t weather) {
    switch (weather) {
        case WIND_RENDERER_WEATHER_CLEAR_DAY:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18, BOOTSTRAP_SUN_18);
            break;
        case WIND_RENDERER_WEATHER_CLEAR_NIGHT:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18, BOOTSTRAP_MOON_18);
            break;
        case WIND_RENDERER_WEATHER_PARTLY_CLOUDY_DAY:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18, BOOTSTRAP_CLOUD_SUN_18);
            break;
        case WIND_RENDERER_WEATHER_PARTLY_CLOUDY_NIGHT:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18,
                            BOOTSTRAP_CLOUD_MOON_18);
            break;
        case WIND_RENDERER_WEATHER_CLOUDY:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18, BOOTSTRAP_CLOUD_18);
            break;
        case WIND_RENDERER_WEATHER_LIGHT_RAIN:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18,
                            BOOTSTRAP_CLOUD_DRIZZLE_18);
            break;
        case WIND_RENDERER_WEATHER_RAIN:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18,
                            BOOTSTRAP_CLOUD_RAIN_18);
            break;
        case WIND_RENDERER_WEATHER_HEAVY_RAIN:
            draw_alpha_icon(canvas, center_x, center_y, 18, 18,
                            BOOTSTRAP_CLOUD_RAIN_HEAVY_18);
            break;
        default:
            break;
    }
}

static int graph_label_baseline(int y, int plot_top, int plot_bottom) {
    /* Keep peak labels inside the plot, clear of the direction row. */
    return y - 7 - MODULE_LABEL_HEIGHT < plot_top
        ? clamp_int(y + MODULE_LABEL_HEIGHT + 5, plot_top + MODULE_LABEL_HEIGHT, plot_bottom - 5)
        : y - 7;
}

static int wind_label_baseline(int y, const dashboard_layout_t *layout) {
    const int plot_top = layout->wind_baseline - layout->chart_scale_height;
    return layout->inset_labels ? graph_label_baseline(y, plot_top, layout->wind_baseline) :
        clamp_int(y - 5, plot_top - 4, layout->wind_baseline - 5);
}

static void draw_sample(canvas_t *canvas, int center_x,
                        const wind_renderer_sample_t *sample,
                        const dashboard_layout_t *layout) {
    char value[12];
    if (!sample->available) {
        const wind_text_metrics_t missing = wind_font_measure(
            WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS, "--");
        draw_text(canvas, center_x - missing.width / 2, layout->wind_baseline,
                  WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS, "--");
        return;
    }

    draw_arrow(canvas, center_x, layout->direction_center, sample->destination_degrees);

    const int sustained = clamp_int(sample->sustained_kt, 0, 40);
    const int gust = clamp_int(sample->gust_kt, 0, 40);
    const int sustained_height = sustained * layout->chart_scale_height / 40;
    const int sustained_y = layout->wind_baseline - sustained_height;
    const int gust_y = layout->wind_baseline - gust * layout->chart_scale_height / 40;
    fill_rect(canvas, center_x - SUSTAINED_BAR_WIDTH / 2, sustained_y,
              SUSTAINED_BAR_WIDTH, sustained_height, CANVAS_BLACK);
    const int gust_gap = (gust - sustained) * layout->chart_scale_height / 40;
    if (gust > sustained && gust_gap >= 6)
        horizontal_line(canvas, center_x - 8, center_x + 7, gust_y, CANVAS_BLACK);

    snprintf(value, sizeof(value), "%d", clamp_int(sample->sustained_kt, 0, 999));
    const int label_baseline = wind_label_baseline(sustained_y, layout);
    draw_outlined_text_center(canvas, center_x, label_baseline,
                              WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
                              WIND_FONT_SIZE_STATUS, value);
}

static void draw_temperature(canvas_t *canvas, int center_x, int row_top,
                             int row_bottom, const wind_renderer_sample_t *sample,
                             int temperature_fahrenheit, int text_center) {
    if (!sample->temperature_available) return;
    char value[12];
    const int whole_degrees =
        temperature_fahrenheit
            ? divide_rounded(sample->temperature_tenths_c * 9, 50) + 32
            : divide_rounded(sample->temperature_tenths_c, 10);
    snprintf(value, sizeof(value), "%d°", whole_degrees);
    const wind_text_metrics_t value_metrics = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS, value);
    const int row_height = row_bottom - row_top + 1;
    const int baseline =
        text_center > 0 ? module_text_baseline(text_center) :
        row_top + (row_height + value_metrics.ascent - value_metrics.descent) / 2;
    draw_text(canvas, center_x - value_metrics.width / 2, baseline,
              WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS, value);
}

static void draw_line(canvas_t *canvas, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        set_pixel(canvas, x0, y0, CANVAS_BLACK);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

/* Subpixel coverage keeps curved strokes smooth at the native display size.
 * Both marine graphs use the same stroke rasterizer and round joins. */
static void draw_curve_segment_color(canvas_t *canvas, double x0, double y0,
                                      double x1, double y1, int width, uint8_t color, int clip_left, int clip_right) {
    const double dx = x1 - x0, dy = y1 - y0;
    const double length_squared = dx * dx + dy * dy;
    const double radius = width / 2.0;
    for (int y = (int)floor(fmin(y0, y1) - radius); y <= (int)ceil(fmax(y0, y1) + radius); ++y) {
        for (int x = (int)floor(fmin(x0, x1) - radius); x <= (int)ceil(fmax(x0, x1) + radius); ++x) {
            if (x <= OUTER_X || x >= OUTER_RIGHT || x < clip_left || x > clip_right || y < 0 || y >= canvas->height) continue;
            double t = length_squared > 0 ? ((x-x0)*dx + (y-y0)*dy) / length_squared : 0;
            t = fmin(1.0, fmax(0.0, t));
            const double distance = hypot(x - x0 - t*dx, y - y0 - t*dy);
            double coverage = fmin(1.0, fmax(0.0, radius + 0.5 - distance));
            if (!canvas->antialias_text) coverage = coverage >= 0.5 ? 1.0 : 0.0;
            const uint8_t previous = canvas->pixels[y * WIND_RENDERER_WIDTH + x];
            const uint8_t gray = color == CANVAS_WHITE
                ? (uint8_t)lround(previous + (255 - previous) * coverage)
                : (uint8_t)lround(255 * (1.0 - coverage));
            if (color == CANVAS_WHITE || gray < previous) set_pixel(canvas, x, y, gray);
        }
    }
}

static void draw_curve_segment(canvas_t *canvas, double x0, double y0,
                                double x1, double y1, int width) {
    draw_curve_segment_color(canvas, x0, y0, x1, y1, width, CANVAS_BLACK, OUTER_X + 1, OUTER_RIGHT - 1);
}

static void draw_tide_curve(canvas_t *canvas, const int *point_x, const int *point_y,
                            int point_count, int clip_left, int clip_right,
                            int curve_top, int curve_bottom) {
    for (int segment = 0; segment + 1 < point_count; ++segment) {
        const int x0 = point_x[segment];
        const int x1 = point_x[segment + 1];
        if (x1 <= x0 || x1 < clip_left || x0 > clip_right) continue;

        const int first_x = clamp_int(clip_left, x0, x1);
        const int last_x = clamp_int(clip_right, x0, x1);
        const float segment_width = (float)(x1 - x0);
        const float start_slope =
            segment == 0
                ? (float)(point_y[segment + 1] - point_y[segment]) / segment_width
                : (float)(point_y[segment + 1] - point_y[segment - 1]) /
                      (float)(point_x[segment + 1] - point_x[segment - 1]);
        const float end_slope =
            segment + 2 >= point_count
                ? (float)(point_y[segment + 1] - point_y[segment]) / segment_width
                : (float)(point_y[segment + 2] - point_y[segment]) /
                      (float)(point_x[segment + 2] - point_x[segment]);

        int previous_x = first_x;
        double previous_y = point_y[segment];
        for (int x = first_x; x <= last_x; ++x) {
            const float t = (float)(x - x0) / segment_width;
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            const float curve_y = h00 * point_y[segment] +
                                                 h10 * segment_width * start_slope +
                                                 h01 * point_y[segment + 1] +
                                                 h11 * segment_width * end_slope;
            const double y = canvas->smooth_curves ? fmin(curve_bottom, fmax(curve_top, curve_y)) :
                clamp_int((int)lroundf(curve_y), curve_top, curve_bottom);
            if (x == first_x) {
                previous_y = y;
            } else {
                if (canvas->smooth_curves) draw_curve_segment(canvas, previous_x, previous_y, x, y, 1);
                else draw_line(canvas, previous_x, previous_y, x, y);
            }
            previous_x = x;
            previous_y = y;
        }
    }
}

static void draw_tide_time_label(canvas_t *canvas,
                                 const wind_renderer_dashboard_t *dashboard,
                                 const dashboard_layout_t *layout, int day, int hour,
                                 int minute, bool is_high, bool show_minutes,
                                 int center_x, int extremum_y) {
    if (hour < FORECAST_FIRST_HOUR || hour > FORECAST_LAST_HOUR) return;
    char time[8];
    if (dashboard->use_24_hour) {
        snprintf(time, sizeof(time), "%02d:%02d", hour, minute);
    } else {
        const int hour_12 = hour % 12 == 0 ? 12 : hour % 12;
        if (show_minutes) {
            snprintf(time, sizeof(time), "%d:%02d%s", hour_12, minute,
                     hour < 12 ? "AM" : "PM");
        } else {
            snprintf(time, sizeof(time), "%d%s", hour_12, hour < 12 ? "AM" : "PM");
        }
    }

    const wind_text_metrics_t metrics = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS, time);
    const int label_margin = metrics.width / 2 + 10;
    const int label_center = clamp_int(center_x, day_column_x(day) + label_margin,
                                       day_column_x(day + 1) - label_margin);
    const int baseline = dashboard->custom_modules
        ? module_text_baseline(is_high ? module_text_center(layout->tide_top, 0, false) :
            layout->tide_bottom + 1 - MODULE_PADDING - MODULE_TEXT_CELL / 2)
        : clamp_int(is_high ? extremum_y - 3 : extremum_y + 16,
                    layout->tide_top + 14, layout->tide_bottom - 5);
    draw_outlined_text_center(canvas, label_center, baseline,
                              WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
                              WIND_FONT_SIZE_STATUS, time);
}

static void draw_tide(canvas_t *canvas, const wind_renderer_dashboard_t *dashboard,
                      const dashboard_layout_t *layout) {
    if (!dashboard->tide_available || dashboard->tide_sample_count < 2) return;

    const int curve_inset = MODULE_PADDING + MODULE_TEXT_CELL + MODULE_TIDE_LABEL_GAP;
    const int curve_top = layout->tide_top + (dashboard->custom_modules ? curve_inset : 25);
    const int curve_bottom = dashboard->custom_modules
        ? layout->tide_bottom + 1 - curve_inset : layout->tide_bottom - 25;
    const int curve_middle = (curve_top + curve_bottom) / 2;

    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        int points[WIND_RENDERER_MAX_TIDE_SAMPLES] = {0};
        int point_x[WIND_RENDERER_MAX_TIDE_SAMPLES] = {0};
        int point_y[WIND_RENDERER_MAX_TIDE_SAMPLES] = {0};
        int point_count = 0;
        int minimum = 0;
        int maximum = 0;

        /* Select in local-hour order so renderer output does not depend on
         * provider ordering. Hours outside the forecast labels are real,
         * visible curve data for the left and right margins. */
        for (int hour = TIDE_DATA_FIRST_HOUR; hour <= TIDE_DATA_LAST_HOUR; ++hour) {
            for (int index = 0; index < dashboard->tide_sample_count; ++index) {
                const wind_renderer_tide_sample_t *sample =
                    &dashboard->tide_samples[index];
                if (!sample->available || sample->day_index != day ||
                    sample->local_hour != hour) {
                    continue;
                }

                points[point_count] = index;
                point_x[point_count] = tide_hour_x(day, hour);
                if (point_count == 0) minimum = maximum = sample->sea_level_mm;
                if (sample->sea_level_mm < minimum) minimum = sample->sea_level_mm;
                if (sample->sea_level_mm > maximum) maximum = sample->sea_level_mm;
                point_count++;
                break;
            }
        }
        if (point_count < 2) continue;

        const int range = maximum - minimum;
        for (int position = 0; position < point_count; ++position) {
            const int value = dashboard->tide_samples[points[position]].sea_level_mm;
            point_y[position] =
                range == 0
                    ? curve_middle
                    : curve_bottom -
                          divide_rounded((value - minimum) * (curve_bottom - curve_top),
                                         range);
        }

        int extrema[WIND_RENDERER_MAX_TIDE_SAMPLES] = {0};
        bool extrema_is_high[WIND_RENDERER_MAX_TIDE_SAMPLES] = {false};
        int extrema_count = 0;
        const int turn_threshold = clamp_int((maximum - minimum) / 10, 10, 500);
        int direction = 0;
        int high_candidate = 0;
        int low_candidate = 0;

        for (int position = 1; position < point_count; ++position) {
            const int value = dashboard->tide_samples[points[position]].sea_level_mm;
            if (direction >= 0 &&
                value >= dashboard->tide_samples[points[high_candidate]].sea_level_mm) {
                high_candidate = position;
            }
            if (direction <= 0 &&
                value <= dashboard->tide_samples[points[low_candidate]].sea_level_mm) {
                low_candidate = position;
            }

            if (direction == 0) {
                if (value -
                        dashboard->tide_samples[points[low_candidate]].sea_level_mm >=
                    turn_threshold) {
                    if (low_candidate > 0) {
                        extrema[extrema_count] = low_candidate;
                        extrema_is_high[extrema_count++] = false;
                    }
                    direction = 1;
                    high_candidate = position;
                } else if (dashboard->tide_samples[points[high_candidate]]
                                   .sea_level_mm -
                               value >=
                           turn_threshold) {
                    if (high_candidate > 0) {
                        extrema[extrema_count] = high_candidate;
                        extrema_is_high[extrema_count++] = true;
                    }
                    direction = -1;
                    low_candidate = position;
                }
            } else if (direction > 0 &&
                       dashboard->tide_samples[points[high_candidate]].sea_level_mm -
                               value >=
                           turn_threshold) {
                extrema[extrema_count] = high_candidate;
                extrema_is_high[extrema_count++] = true;
                direction = -1;
                low_candidate = position;
            } else if (direction < 0 &&
                       value - dashboard->tide_samples[points[low_candidate]]
                                   .sea_level_mm >=
                           turn_threshold) {
                extrema[extrema_count] = low_candidate;
                extrema_is_high[extrema_count++] = false;
                direction = 1;
                high_candidate = position;
            }
        }

        if (extrema_count > 0) {
            const int previous = extrema[extrema_count - 1];
            if (direction > 0 && high_candidate > previous &&
                high_candidate < point_count - 1) {
                extrema[extrema_count] = high_candidate;
                extrema_is_high[extrema_count++] = true;
            } else if (direction < 0 && low_candidate > previous &&
                       low_candidate < point_count - 1) {
                extrema[extrema_count] = low_candidate;
                extrema_is_high[extrema_count++] = false;
            }
        }

        const int first_x = day_column_x(day) + 1;
        const int last_x = day_column_x(day + 1) - 1;
        draw_tide_curve(canvas, point_x, point_y, point_count, first_x, last_x,
                        curve_top, curve_bottom);

        bool has_explicit_extrema = false;
        for (int extremum = 0; extremum < dashboard->tide_extremum_count; ++extremum) {
            const wind_renderer_tide_extremum_t *event =
                &dashboard->tide_extrema[extremum];
            if (!event->available || event->day_index != day) continue;
            has_explicit_extrema = true;
            const int event_y =
                range == 0
                    ? curve_middle
                    : clamp_int(curve_bottom -
                                    divide_rounded((event->sea_level_mm - minimum) *
                                                       (curve_bottom - curve_top),
                                                   range),
                                curve_top, curve_bottom);
            draw_tide_time_label(
                canvas, dashboard, layout, day, event->local_hour, event->local_minute,
                event->is_high != 0, true,
                tide_time_x(day, event->local_hour, event->local_minute), event_y);
        }
        if (has_explicit_extrema) continue;

        for (int extremum = 0; extremum < extrema_count; ++extremum) {
            const int position = extrema[extremum];
            const int index = points[position];
            draw_tide_time_label(canvas, dashboard, layout, day,
                                 dashboard->tide_samples[index].local_hour, 0,
                                 extrema_is_high[extremum], false, point_x[position],
                                 point_y[position]);
        }
    }
}

static void build_footer_status(const wind_renderer_dashboard_t *dashboard,
                                char *status, size_t status_size) {
    const char *provider = safe_text(dashboard->provider);
    const char *updated = safe_text(dashboard->updated_time);
    if (dashboard->state == WIND_RENDERER_UNAVAILABLE) {
        snprintf(status, status_size, "UNAVAILABLE // %s", updated);
    } else if (dashboard->state == WIND_RENDERER_STALE) {
        snprintf(status, status_size, "%s // STALE %dh // %s", provider,
                 dashboard->age_hours, updated);
    } else if (dashboard->refresh_failed) {
        snprintf(status, status_size, "%s // OFFLINE // %s", provider, updated);
    } else if (dashboard->state == WIND_RENDERER_AGED) {
        snprintf(status, status_size, "%s // AGED %dh // %s", provider,
                 dashboard->age_hours, updated);
    } else {
        snprintf(status, status_size, "%s // %s", provider, updated);
    }
}

static void build_header_update(const wind_renderer_dashboard_t *dashboard,
                                char *update, size_t update_size) {
    const char *updated = safe_text(dashboard->updated_time);
    if (dashboard->state == WIND_RENDERER_UNAVAILABLE) {
        snprintf(update, update_size, "UNAVAILABLE // %s", updated);
    } else if (dashboard->state == WIND_RENDERER_STALE) {
        snprintf(update, update_size, "STALE %dh // %s", dashboard->age_hours, updated);
    } else if (dashboard->refresh_failed) {
        snprintf(update, update_size, "OFFLINE // %s", updated);
    } else if (dashboard->state == WIND_RENDERER_AGED) {
        snprintf(update, update_size, "AGED %dh // %s", dashboard->age_hours, updated);
    } else {
        snprintf(update, update_size, "%s", updated);
    }
}

static void build_time_axis_label(const wind_renderer_dashboard_t *dashboard,
                                  const wind_renderer_sample_t *sample, char *label,
                                  size_t label_size) {
    const char *time = safe_text(sample->time);
    if (!sample->available || !time[0]) {
        label[0] = '\0';
    } else if (dashboard->use_24_hour) {
        snprintf(label, label_size, "%sh", time);
    } else {
        copy_bounded_text(label, label_size, time);
    }
}

static void draw_footer(canvas_t *canvas, const wind_renderer_dashboard_t *dashboard) {
    if (!dashboard->show_dedicated_footer) return;
    const int footer_top = canvas_footer_top(canvas);
    const int row_top = footer_top + 1;
    const int row_bottom = canvas->height - 1;
    const int row_height = row_bottom - row_top + 1;
    const wind_text_metrics_t footer_metrics = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_FOOTER, "");
    const int footer_text_baseline =
        row_top + (row_height + footer_metrics.ascent - footer_metrics.descent) / 2 +
        FOOTER_TEXT_OFFSET_Y;
    const int footer_battery_center_y = row_top + (row_height - 1) / 2;

    horizontal_line(canvas, OUTER_X, OUTER_RIGHT, footer_top, CANVAS_BLACK);

    char status[104];
    build_footer_status(dashboard, status, sizeof(status));
    const wind_text_metrics_t status_metrics = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_FOOTER, status);
    const int status_right = CONTENT_RIGHT - 34;
    const int status_left = status_right - status_metrics.width + 1;

    if (dashboard->state != WIND_RENDERER_UNAVAILABLE) {
        for (int day = 0; day < FOOTER_TIME_DAY_COUNT; ++day) {
            char last_label[WIND_RENDERER_TIME_LABEL_CAPACITY + 2];
            build_time_axis_label(
                dashboard,
                &dashboard->days[day].samples[WIND_RENDERER_SAMPLES_PER_DAY - 1],
                last_label, sizeof(last_label));
            const wind_text_metrics_t last_metrics =
                wind_font_measure(WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
                                  WIND_FONT_SIZE_FOOTER, last_label);
            const int last_label_right =
                forecast_sample_center_x(day, WIND_RENDERER_SAMPLES_PER_DAY - 1) +
                (last_metrics.width + 1) / 2;
            if (last_label_right + FOOTER_STATUS_GAP > status_left) break;

            for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
                char label[WIND_RENDERER_TIME_LABEL_CAPACITY + 2];
                build_time_axis_label(dashboard, &dashboard->days[day].samples[sample],
                                      label, sizeof(label));
                if (!label[0]) continue;
                const wind_text_metrics_t metrics =
                    wind_font_measure(WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
                                      WIND_FONT_SIZE_FOOTER, label);
                draw_text(canvas,
                          forecast_sample_center_x(day, sample) - metrics.width / 2,
                          footer_text_baseline, WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
                          WIND_FONT_SIZE_FOOTER, label);
            }
        }
    }

    draw_text_right(canvas, status_right, footer_text_baseline,
                    WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_FOOTER,
                    status);
    draw_battery(canvas, CONTENT_RIGHT, footer_battery_center_y,
                 dashboard->battery_percent);
}

static void draw_header_status(canvas_t *canvas,
                               const wind_renderer_dashboard_t *dashboard) {
    char update[48];
    build_header_update(dashboard, update, sizeof(update));
    draw_battery(canvas, CONTENT_RIGHT, 36, dashboard->battery_percent);
    draw_text_right(canvas, CONTENT_RIGHT, HEADER_TEXT_BASELINE,
                    WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS,
                    update);
}

/* Compact decimal spacing is local to wave heights; other mono text stays unchanged. */
static void draw_swell_height(canvas_t *canvas, int center, int baseline, int height_cm) {
    char value[16];
    if (height_cm < 0) snprintf(value, sizeof(value), "-");
    else {
        const int tenths = (height_cm + 5) / 10;
        if (tenths >= 100 && tenths % 10 == 0) snprintf(value, sizeof(value), "%d", tenths / 10);
        else snprintf(value, sizeof(value), "%d.%d", tenths / 10, tenths % 10);
    }
    const wind_font_family_t font = WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED;
    const int size = WIND_FONT_SIZE_STATUS;
    int width = 0;
    for (const char *p = value; *p; ++p) {
        char glyph[2] = {*p, 0};
        width += *p == '.' ? 3 : wind_font_measure(font, size, glyph).width;
    }
    for (int pass = 0; pass < 2; ++pass) {
        int x = center - width / 2;
        for (const char *p = value; *p; ++p) {
            char glyph[2] = {*p, 0};
            const int advance = wind_font_measure(font, size, glyph).width;
            const int glyph_x = *p == '.' ? x - (advance - 3) / 2 : x;
            if (pass == 0) {
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (dx * dx + dy * dy <= 4)
                            draw_text_color(canvas, glyph_x + dx, baseline + dy, font, size, CANVAS_WHITE, glyph);
            } else draw_text(canvas, glyph_x, baseline, font, size, glyph);
            x += *p == '.' ? 3 : advance;
        }
    }
}

static void draw_heavy_line(canvas_t *canvas, int x0, int y0, int x1, int y1) {
    draw_line(canvas, x0, y0, x1, y1);
    draw_line(canvas, x0, y0 + 1, x1, y1 + 1);
}

static void draw_swell_arrow_scaled(canvas_t *canvas, int x, int y, int degrees, double scale) {
    /* Filled head and shaft match the optical weight of the wind arrow,
     * inside the same 20px icon slot. */
    const double angle = degrees * 3.14159265358979323846 / 180.0;
    const double sine = sin(angle), cosine = cos(angle);
    static const double source[7][2] = {
        {-1.5, 7}, {1.5, 7}, {-1.5, -1}, {1.5, -1},
        {-5, -1}, {0, -8}, {5, -1},
    };
    int points[7][2];
    for (int i = 0; i < 7; ++i) {
        points[i][0] = x + (int)lround(scale * (source[i][0] * cosine - source[i][1] * sine));
        points[i][1] = y + (int)lround(scale * (source[i][0] * sine + source[i][1] * cosine));
    }
    fill_triangle(canvas, points[0][0], points[0][1], points[1][0], points[1][1], points[2][0], points[2][1]);
    fill_triangle(canvas, points[1][0], points[1][1], points[2][0], points[2][1], points[3][0], points[3][1]);
    fill_triangle(canvas, points[4][0], points[4][1], points[5][0], points[5][1], points[6][0], points[6][1]);
}

static void draw_centered_value(canvas_t *canvas, int x, int baseline, int size,
                                const char *value, uint8_t gray) {
    const wind_text_metrics_t metrics = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, size, value);
    draw_text_color(canvas, x - metrics.width / 2, baseline,
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, size, gray, value);
}

/* A fixed soft-log scale includes calm water without log(0), and never
 * rescales when switching spots or forecasts. Values above 10m get overflow marks. */
static double swell_y(double height_cm, int top, int bottom) {
    const double height = fmin(fmax(height_cm, 0.0), SWELL_SCALE_CM);
    return bottom - log1p(height / 50.0) / log1p(SWELL_SCALE_CM / 50.0) * (bottom - top);
}

static double swell_tangent(int before, int current, int after) {
    const double a = current - before, b = after - current;
    if (a * b <= 0) return 0;
    return 2 * a * b / (a + b);
}

static double swell_interpolate(const int *hours, int hour, double t) {
    const double start = hours[hour], end = hours[hour + 1];
    const double m0 = hour > 0 && hours[hour - 1] >= 0
        ? swell_tangent(hours[hour - 1], start, end) : end - start;
    const double m1 = hour < 22 && hours[hour + 2] >= 0
        ? swell_tangent(start, end, hours[hour + 2]) : end - start;
    const double t2 = t * t, t3 = t2 * t;
    return (2*t3 - 3*t2 + 1)*start + (t3 - 2*t2 + t)*m0 +
           (-2*t3 + 3*t2)*end + (t3 - t2)*m1;
}

static void draw_swell_trace(canvas_t *canvas, const int *hours, int day,
                              int plot_top, int plot_bottom, int width, uint8_t color, bool dashed) {
    for (int hour = TIDE_DATA_FIRST_HOUR; hour < TIDE_DATA_LAST_HOUR; ++hour) {
        if (hours[hour] < 0 || hours[hour + 1] < 0) continue;
        const int x0 = tide_time_x(day, hour, 0), x1 = tide_time_x(day, hour + 1, 0);
        const int left = clamp_int(x0, day_column_x(day) + 1, day_column_x(day + 1) - 1);
        const int right = clamp_int(x1, day_column_x(day) + 1, day_column_x(day + 1) - 1);
        double previous_y = 0;
        for (int x = left; x <= right; ++x) {
            const double height = swell_interpolate(hours, hour, (double)(x - x0) / (x1 - x0));
            const double y = swell_y(height, plot_top, plot_bottom);
            if (x > left && (!dashed || (x - day_column_x(day)) % 7 < 3))
                draw_curve_segment_color(canvas, x - 1, previous_y, x, y, width, color, day_column_x(day) + 1, day_column_x(day + 1) - 1);
            previous_y = y;
        }
    }
}

static void draw_swell_module(canvas_t *canvas, const wind_renderer_dashboard_t *dashboard,
                              int top, int bottom, int size) {
    const bool large = size == 2;
    bool secondary = false;
    if (large) {
        for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day)
            for (int hour = TIDE_DATA_FIRST_HOUR; hour <= TIDE_DATA_LAST_HOUR; ++hour)
                if (dashboard->secondary_swell_hourly[day][hour] > 0) secondary = true;
    }
    const int plot_top = top + MODULE_GRAPH_INSET;
    const int plot_bottom = bottom - MODULE_PERIOD_INSET;
    const int direction_y = module_cell_center(top, 0);
    const int period_baseline = module_text_baseline(bottom - MODULE_PADDING - MODULE_TEXT_CELL / 2);
    if (large) {
        const int ticks[] = {0, 250, 500, 750, SWELL_SCALE_CM};
        for (size_t tick = 0; tick < sizeof(ticks) / sizeof(ticks[0]); ++tick) {
            const int y = (int)lround(swell_y(ticks[tick], plot_top, plot_bottom));
            for (int x = OUTER_X + 1; x < OUTER_RIGHT; x += 7) set_pixel(canvas, x, y, CANVAS_BLACK);
        }
    }
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        if (large) {
            if (secondary) draw_swell_trace(canvas, dashboard->secondary_swell_hourly[day], day,
                plot_top, plot_bottom, SWELL_LINE_WIDTH, CANVAS_BLACK, true);
            /* Paint the complete halo before the foreground stroke so segment joins stay intact. */
            if (secondary) draw_swell_trace(canvas, dashboard->swell_hourly[day], day,
                plot_top, plot_bottom, SWELL_LINE_WIDTH + 2, CANVAS_WHITE, false);
            draw_swell_trace(canvas, dashboard->swell_hourly[day], day,
                plot_top, plot_bottom, SWELL_LINE_WIDTH, CANVAS_BLACK, false);
        }
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            const wind_renderer_swell_sample_t *swell = &dashboard->swell[day][sample];
            const int x = forecast_sample_center_x(day, sample);
            char period[12];
            if (large) {
                const int y = swell->height_cm < 0 ? plot_bottom :
                    (int)lround(swell_y(swell->height_cm, plot_top, plot_bottom));
                draw_swell_height(canvas, x, graph_label_baseline(y, plot_top, plot_bottom), swell->height_cm);
                if (swell->height_cm >= 0) {
                    fill_rect(canvas, x - 4, y - 4, 9, 9, CANVAS_WHITE);
                    fill_rect(canvas, x - 2, y - 2, 5, 5, CANVAS_BLACK);
                }
                if (swell->height_cm > SWELL_SCALE_CM) {
                    draw_heavy_line(canvas, x - 3, y + 5, x, y + 2);
                    draw_heavy_line(canvas, x, y + 2, x + 3, y + 5);
                }
            } else draw_swell_height(canvas, x, module_text_baseline(module_cell_center(top, 1)), swell->height_cm);
            if (swell->destination_degrees >= 0) draw_swell_arrow_scaled(canvas, x, direction_y, swell->destination_degrees, 1.0);
            if (swell->period_tenths >= 0) snprintf(period, sizeof(period), "%dS", (int)((swell->period_tenths + 5) / 10));
            else snprintf(period, sizeof(period), "-");
            draw_centered_value(canvas, x, period_baseline, WIND_FONT_SIZE_STATUS, period, CANVAS_BLACK);

        }
    }
}

static dashboard_layout_t draw_wind_module(canvas_t *canvas, const wind_renderer_dashboard_t *dashboard,
                             int top, int bottom, int size) {
    dashboard_layout_t layout = {0};
    layout.wind_baseline = bottom - MODULE_PADDING;
    layout.chart_scale_height = layout.wind_baseline - (top + MODULE_GRAPH_INSET);
    layout.direction_center = module_cell_center(top, 0);
    layout.inset_labels = true;
    if (size == 2) {
        for (int knots = 5; knots <= 40; knots += 5) {
            const int y = layout.wind_baseline - knots * layout.chart_scale_height / 40;
            for (int x = OUTER_X + 1; x < OUTER_RIGHT; x += 7) set_pixel(canvas, x, y, CANVAS_BLACK);
        }
    }
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day)
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            const wind_renderer_sample_t *wind = &dashboard->days[day].samples[sample];
            const int x = forecast_sample_center_x(day, sample);
            if (size == 2) { draw_sample(canvas, x, wind, &layout); continue; }
            char value[12];
            if (wind->available) {
                draw_arrow(canvas, x, module_cell_center(top, 0), wind->destination_degrees);
                snprintf(value, sizeof(value), "%d", wind->sustained_kt);
            } else snprintf(value, sizeof(value), "-");
            draw_centered_value(canvas, x, module_text_baseline(module_cell_center(top, 1)), WIND_FONT_SIZE_STATUS, value, CANVAS_BLACK);
            if (wind->available) snprintf(value, sizeof(value), "%d", wind->gust_kt);
            draw_centered_value(canvas, x, module_text_baseline(module_cell_center(top, 2)), WIND_FONT_SIZE_STATUS, value, CANVAS_BLACK);
        }
    return layout;
}

static void draw_modules(canvas_t *canvas, const wind_renderer_dashboard_t *dashboard,
                                  const dashboard_layout_t *layout, dashboard_layout_t *wind_layout) {
    const int sizes[2] = { dashboard->wind_size, dashboard->swell_size };
    const int large_count = (sizes[0] == 2) + (sizes[1] == 2);
    const int small_count = (sizes[0] == 1) + (sizes[1] == 1);
    const int available = layout->modules_bottom - DAY_HEADER_BOTTOM - small_count * MODULE_COMPACT_HEIGHT;
    int top = DAY_HEADER_BOTTOM;
    /* Large modules lead; equal-size modules retain Wind, Swell order. */
    for (int size = 2; size >= 1; --size) {
        for (int module = 0; module < 2; ++module) {
            if (sizes[module] != size) continue;
            const int height = size == 1 ? MODULE_COMPACT_HEIGHT :
                available / large_count + (large_count == 2 && module == 1 ? available % 2 : 0);
            if (top != DAY_HEADER_BOTTOM) horizontal_line(canvas, OUTER_X, OUTER_RIGHT, top, CANVAS_BLACK);
            if (module == 0) *wind_layout = draw_wind_module(canvas, dashboard, top, top + height, size);
            else draw_swell_module(canvas, dashboard, top, top + height, size);
            top += height;
        }
    }
    if (top > DAY_HEADER_BOTTOM && top < layout->modules_bottom)
        horizontal_line(canvas, OUTER_X, OUTER_RIGHT, top, CANVAS_BLACK);
}

/* Explicit order is independent of module size. Small rows retain their height. */
static void draw_ordered_modules(canvas_t *canvas, const wind_renderer_dashboard_t *dashboard, dashboard_layout_t *wind_layout) {
    int sizes[] = { dashboard->wind_size, dashboard->swell_size,
        dashboard->show_weather, dashboard->show_temperature, dashboard->show_tide };
    int fixed[] = { MODULE_COMPACT_HEIGHT, MODULE_COMPACT_HEIGHT,
        MODULE_PADDING * 2 + MODULE_CELL, MODULE_PADDING * 2 + MODULE_TEXT_CELL, MODULE_TIDE_HEIGHT };
    /* Preserve the shared weather/temperature card when these rows are adjacent. */
    bool combined = false;
    for (int i = 0; i < 4; ++i) {
        if (dashboard->module_order[i] != 2 || !sizes[2] || !sizes[3]) continue;
        int next = i + 1;
        while (next < 5 && !sizes[dashboard->module_order[next]]) ++next;
        combined = next < 5 && dashboard->module_order[next] == 3;
    }
    if (combined) {
        fixed[2] = MODULE_PADDING * 2 + MODULE_CELL + MODULE_TEXT_CELL + MODULE_GAP;
        sizes[3] = 0;
    }
    const int bottom = dashboard->show_dedicated_footer ? canvas_footer_top(canvas) : canvas_outer_bottom(canvas);
    int available = bottom - DAY_HEADER_BOTTOM, large = 0;
    for (int id = 0; id < 5; ++id) {
        if (id < 2 && sizes[id] == 2) ++large;
        else if (sizes[id]) available -= fixed[id];
    }
    int top = DAY_HEADER_BOTTOM, remaining = large;
    for (int index = 0; index < 5; ++index) {
        const int id = dashboard->module_order[index];
        if (!sizes[id]) continue;
        int height = fixed[id];
        if (id < 2 && sizes[id] == 2) {
            height = available / large + (--remaining == 0 ? available % large : 0);
        }
        if (top != DAY_HEADER_BOTTOM) horizontal_line(canvas, OUTER_X, OUTER_RIGHT, top, CANVAS_BLACK);
        if (id == 0) *wind_layout = draw_wind_module(canvas, dashboard, top, top + height, sizes[id]);
        else if (id == 1) draw_swell_module(canvas, dashboard, top, top + height, sizes[id]);
        else if (id == 4) {
            const dashboard_layout_t row = { .tide_top = top, .tide_bottom = top + height - 1 };
            draw_tide(canvas, dashboard, &row);
        } else {
            for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
                for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
                    const int x = forecast_sample_center_x(day, sample);
                    const wind_renderer_sample_t *slot = &dashboard->days[day].samples[sample];
                    if (id == 2) {
                        draw_weather(canvas, x, module_cell_center(top, 0), slot->weather);
                        if (combined) draw_temperature(canvas, x, top, top + height - 1, slot,
                            dashboard->temperature_fahrenheit, module_text_center(top, 0, true));
                    }
                    else draw_temperature(canvas, x, top, top + height - 1, slot,
                        dashboard->temperature_fahrenheit, module_text_center(top, 0, false));
                }
            }
        }
        top += height;
    }
    if (top > DAY_HEADER_BOTTOM && top < bottom) horizontal_line(canvas, OUTER_X, OUTER_RIGHT, top, CANVAS_BLACK);
}

static void draw_wind_reference_lines(canvas_t *canvas,
                                      const dashboard_layout_t *layout) {
    const int graph_height = layout->wind_baseline - GRAPH_TOP;
    for (int knots = 5; knots <= 40; knots += 5) {
        const int y = layout->wind_baseline - (knots * graph_height + 20) / 40;
        for (int x = OUTER_X; x <= OUTER_RIGHT; x += 7) {
            horizontal_line(canvas, x, x, y, CANVAS_BLACK);
        }
        horizontal_line(canvas, OUTER_RIGHT, OUTER_RIGHT, y, CANVAS_BLACK);
    }
}

static void draw_battery(canvas_t *canvas, int right, int center_y, int percent) {
    const int body_width = 21;
    const int body_height = 11;
    const int x = right - body_width - 2;
    const int y = center_y - body_height / 2;
    outline_rect(canvas, x, y, body_width, body_height);
    fill_rect(canvas, right - 1, center_y - 2, 2, 5, CANVAS_BLACK);
    if (percent >= 0) {
        const int interior_width = body_width - 4;
        const int clamped_percent = clamp_int(percent, 0, 100);
        const int fill = clamped_percent >= 99 ? interior_width
                                               : clamped_percent * interior_width / 100;
        fill_rect(canvas, x + 2, y + 2, fill, body_height - 4, CANVAS_BLACK);
    }
}

static int divide_rounded(int value, int divisor) {
    if (value >= 0) return (value + divisor / 2) / divisor;
    return -((-value + divisor / 2) / divisor);
}

static int dither_once(uint8_t *luma, uint8_t *palette, int height) {
    int *current = (int *)calloc((size_t)WIND_RENDERER_WIDTH + 2u, sizeof(int));
    int *next = (int *)calloc((size_t)WIND_RENDERER_WIDTH + 2u, sizeof(int));
    if (!current || !next) {
        free(current);
        free(next);
        return -1;
    }

    for (int y = 0; y < height; ++y) {
        memset(next, 0, ((size_t)WIND_RENDERER_WIDTH + 2u) * sizeof(int));
        for (int x = 0; x < WIND_RENDERER_WIDTH; ++x) {
            const int index = y * WIND_RENDERER_WIDTH + x;
            const int adjusted = clamp_int(
                (int)luma[index] + divide_rounded(current[x + 1], 16), 0, 255);
            const int target = adjusted < 128 ? 0 : 255;
            const int error = adjusted - target;
            palette[index] = target == 0 ? PALETTE_BLACK : PALETTE_WHITE;
            current[x + 2] += error * 7;
            next[x] += error * 3;
            next[x + 1] += error * 5;
            next[x + 2] += error;
        }
        int *swap = current;
        current = next;
        next = swap;
    }
    free(current);
    free(next);
    return 0;
}

static void set_output_pixel(output_surface_t *output, int index,
                             output_color_t color) {
    if (output->format == OUTPUT_PALETTE) {
        output->pixels[index] = color == OUTPUT_BLACK   ? PALETTE_BLACK
                                : color == OUTPUT_WHITE ? PALETTE_WHITE
                                                        : PALETTE_RED;
        return;
    }
    if (output->format == OUTPUT_GRAY4) {
        output->pixels[index] = color == OUTPUT_BLACK   ? 0
                                : color == OUTPUT_WHITE ? 3
                                                        : 1;
        return;
    }
    if (output->format == OUTPUT_GC16) {
        output->pixels[index] = color == OUTPUT_BLACK   ? 0
                                : color == OUTPUT_WHITE ? 15
                                                        : 5;
        return;
    }
    const int offset = index * 4;
    if (output->format == OUTPUT_RGBA_GC16) {
        const uint8_t luma = color == OUTPUT_BLACK   ? 0
                             : color == OUTPUT_WHITE ? 255
                                                     : 85;
        output->pixels[offset] = luma;
        output->pixels[offset + 1] = luma;
        output->pixels[offset + 2] = luma;
        output->pixels[offset + 3] = 255;
        return;
    }
    output->pixels[offset] = color == OUTPUT_BLACK ? 0 : 255;
    output->pixels[offset + 1] = color == OUTPUT_RED || color == OUTPUT_BLACK ? 0 : 255;
    output->pixels[offset + 2] = color == OUTPUT_RED || color == OUTPUT_BLACK ? 0 : 255;
    output->pixels[offset + 3] = 255;
}

static void apply_mask_to_output(const canvas_t *mask, output_surface_t *output,
                                 int left, int top, int right, int bottom,
                                 output_color_t color) {
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (mask->pixels[y * WIND_RENDERER_WIDTH + x] < 128)
                set_output_pixel(output, y * WIND_RENDERER_WIDTH + x, color);
        }
    }
}

static void draw_output_outlined_text(canvas_t *scratch, output_surface_t *output,
                                      int x, int baseline, wind_font_family_t family,
                                      int size, const char *text,
                                      output_color_t text_color) {
    const wind_text_metrics_t metrics = wind_font_measure(family, size, text);
    const int mask_left = x - 3;
    const int mask_top = baseline - 24;
    const int mask_right = x + metrics.width + 3;
    const int mask_bottom = baseline + 4;

    memset(scratch->pixels, CANVAS_WHITE, scratch->size);
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if ((dx == 0 && dy == 0) || dx * dx + dy * dy > 4) continue;
            draw_text_mask(scratch, x + dx, baseline + dy, family, size, text);
        }
    }
    apply_mask_to_output(scratch, output, mask_left, mask_top, mask_right, mask_bottom,
                         OUTPUT_WHITE);

    memset(scratch->pixels, CANVAS_WHITE, scratch->size);
    draw_text_mask(scratch, x, baseline, family, size, text);
    apply_mask_to_output(scratch, output, mask_left, mask_top, mask_right, mask_bottom,
                         text_color);
}

static void draw_threshold_overlay(canvas_t *scratch, output_surface_t *output,
                                   const wind_renderer_dashboard_t *dashboard,
                                   const dashboard_layout_t *layout) {
    const int y = layout->wind_baseline -
                  dashboard->threshold_kt * layout->chart_scale_height / 40;
    const int line_left = forecast_sample_center_x(0, 0) - SUSTAINED_BAR_WIDTH / 2 - 2;
    const int line_right = forecast_sample_center_x(WIND_RENDERER_DAY_COUNT - 1,
                                                    WIND_RENDERER_SAMPLES_PER_DAY - 1) +
                           SUSTAINED_BAR_WIDTH / 2 - 1 + 2;
    for (int x = line_left; x <= line_right; ++x) {
        set_output_pixel(output, (y - 1) * WIND_RENDERER_WIDTH + x, OUTPUT_WHITE);
        set_output_pixel(output, y * WIND_RENDERER_WIDTH + x, OUTPUT_RED);
        set_output_pixel(output, (y + 1) * WIND_RENDERER_WIDTH + x, OUTPUT_WHITE);
    }

    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            const wind_renderer_sample_t *source =
                &dashboard->days[day].samples[sample];
            if (!source->available) continue;
            const int center_x = forecast_sample_center_x(day, sample);
            const int sustained = clamp_int(source->sustained_kt, 0, 40);
            const int sustained_y =
                layout->wind_baseline - sustained * layout->chart_scale_height / 40;
            const int baseline = wind_label_baseline(sustained_y, layout);
            if (baseline - 14 > y || baseline + 3 < y) continue;

            char value[12];
            snprintf(value, sizeof(value), "%d",
                     clamp_int(source->sustained_kt, 0, 999));
            const wind_text_metrics_t metrics = wind_font_measure(
                WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS, value);
            draw_output_outlined_text(scratch, output, center_x - metrics.width / 2,
                                      baseline, WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
                                      WIND_FONT_SIZE_STATUS, value, OUTPUT_BLACK);
        }
    }

    char label[16];
    snprintf(label, sizeof(label), "%dKTS", dashboard->threshold_kt);
    const int baseline = y + 5;
    const wind_text_metrics_t metrics = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, WIND_FONT_SIZE_STATUS, label);
    const int x = CONTENT_RIGHT - metrics.width + 1;
    draw_output_outlined_text(scratch, output, x, baseline,
                              WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
                              WIND_FONT_SIZE_STATUS, label, OUTPUT_RED);
}

static void draw_low_battery_overlay(canvas_t *scratch, output_surface_t *output,
                                     int battery_percent, bool show_dedicated_footer) {
    if (battery_percent < 0 || battery_percent >= 10) return;
    int battery_center_y = 50;
    if (show_dedicated_footer) {
        const int row_top = canvas_footer_top(scratch) + 1;
        const int row_bottom = scratch->height - 1;
        const int row_height = row_bottom - row_top + 1;
        battery_center_y = row_top + (row_height - 1) / 2;
    }

    memset(scratch->pixels, CANVAS_WHITE, scratch->size);
    draw_battery(scratch, CONTENT_RIGHT, battery_center_y, battery_percent);
    apply_mask_to_output(scratch, output, CONTENT_RIGHT - 24, battery_center_y - 7,
                         CONTENT_RIGHT + 1, battery_center_y + 6, OUTPUT_RED);
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
    memcpy(result.secondary_swell, input->secondary_swell, sizeof(result.secondary_swell));
    memcpy(result.swell, input->swell, sizeof(result.swell));
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
    if (!dashboard_valid(&result)) return -2;
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

int wind_renderer_palette_row_to_rgb(const uint8_t *palette_row, size_t width,
                                     uint8_t *rgb_row, size_t rgb_size) {
    if (!palette_row || !rgb_row || width == 0 || width > rgb_size / 3) return -1;
    for (size_t x = 0; x < width; ++x)
        if (palette_row[x] != PALETTE_BLACK && palette_row[x] != PALETTE_WHITE &&
            palette_row[x] != PALETTE_RED)
            return -2;
    for (size_t x = 0; x < width; ++x) {
        const bool is_white = palette_row[x] == PALETTE_WHITE;
        const bool is_red = palette_row[x] == PALETTE_RED;
        rgb_row[x * 3] = (is_white || is_red) ? 255 : 0;
        rgb_row[x * 3 + 1] = is_white ? 255 : 0;
        rgb_row[x * 3 + 2] = is_white ? 255 : 0;
    }
    return 0;
}

static int render_dashboard(const wind_renderer_dashboard_t *dashboard,
                            uint8_t *output_pixels, size_t output_size,
                            output_format_t output_format,
                            wind_renderer_stats_t *stats) {
    const int canvas_height =
        output_format == OUTPUT_GC16 || output_format == OUTPUT_RGBA_GC16
            ? WIND_RENDERER_E1003_COMPOSITION_HEIGHT
            : WIND_RENDERER_HEIGHT;
    const size_t canvas_size = (size_t)WIND_RENDERER_WIDTH * canvas_height;
    const size_t required_size =
        output_format == OUTPUT_RGBA || output_format == OUTPUT_RGBA_GC16
            ? canvas_size * 4u
            : canvas_size;
    if (!dashboard || !output_pixels || output_size < required_size) return -1;
    if (!dashboard_valid(dashboard)) return -3;
    if (dashboard->ordered_modules) {
        unsigned seen = 0;
        for (int i = 0; i < 5; ++i) {
            int id = dashboard->module_order[i];
            if (id < 0 || id >= 5 || (seen & (1u << id))) return -3;
            seen |= 1u << id;
        }
    }

    canvas_t canvas = {0};
    canvas.pixels = (uint8_t *)malloc(canvas_size);
    if (!canvas.pixels) return -2;
    canvas.height = canvas_height;
    canvas.smooth_curves = dashboard->custom_modules;
    canvas.size = canvas_size;
    canvas.antialias_text = output_format == OUTPUT_RGBA ||
                            output_format == OUTPUT_RGBA_GC16 ||
                            output_format == OUTPUT_GC16;
    memset(canvas.pixels, CANVAS_WHITE, canvas_size);
    if (stats) memset(stats, 0, sizeof(*stats));
    const dashboard_layout_t layout = dashboard_layout(dashboard, &canvas);

    const int frame_bottom = dashboard->show_dedicated_footer
                                 ? canvas_footer_top(&canvas)
                                 : canvas_outer_bottom(&canvas);
    outline_rect(&canvas, OUTER_X, OUTER_TOP, OUTER_RIGHT - OUTER_X + 1,
                 frame_bottom - OUTER_TOP + 1);
    horizontal_line(&canvas, OUTER_X, OUTER_RIGHT, HEADER_BOTTOM, CANVAS_BLACK);

    if (!dashboard->custom_modules) draw_wind_reference_lines(&canvas, &layout);

    const int header_text_right =
        dashboard->show_dedicated_footer ? CONTENT_RIGHT : 630;
    const int header_width = header_text_right - CONTENT_LEFT + 1;
    char spot_name[WIND_RENDERER_SPOT_NAME_CAPACITY];
    uppercase_spot_name(spot_name, sizeof(spot_name), dashboard->spot_name);
    const wind_text_metrics_t name_metrics =
        wind_font_measure(WIND_FONT_INTER, WIND_FONT_SIZE_SPOT, spot_name);
    draw_text(&canvas, CONTENT_LEFT, HEADER_TEXT_BASELINE, WIND_FONT_INTER,
              WIND_FONT_SIZE_SPOT, spot_name);
    if (name_metrics.width > header_width) {
        const int fade_width = 240;
        const int name_top = HEADER_TEXT_BASELINE - name_metrics.ascent;
        const int name_bottom = HEADER_TEXT_BASELINE + name_metrics.descent;
        const int ink_right = rightmost_ink_pixel(&canvas, CONTENT_LEFT, name_top,
                                                  header_text_right, name_bottom);
        fade_region_to_white(
            &canvas, clamp_int(ink_right - fade_width + 1, CONTENT_LEFT, ink_right),
            name_top, ink_right, name_bottom);
        fill_rect(&canvas, header_text_right + 1, name_top,
                  OUTER_RIGHT - header_text_right - 1, name_bottom - name_top + 1,
                  CANVAS_WHITE);
        fill_rect(&canvas, OUTER_RIGHT + 1, name_top,
                  WIND_RENDERER_WIDTH - OUTER_RIGHT - 1, name_bottom - name_top + 1,
                  CANVAS_WHITE);
    }
    if (!dashboard->show_dedicated_footer) draw_header_status(&canvas, dashboard);
    horizontal_line(&canvas, OUTER_X, OUTER_RIGHT, DAY_HEADER_BOTTOM, CANVAS_BLACK);
    if (!dashboard->ordered_modules && (dashboard->show_weather || dashboard->show_temperature)) {
        const int conditions_top =
            dashboard->show_weather ? layout.weather_top : layout.temperature_top;
        horizontal_line(&canvas, OUTER_X, OUTER_RIGHT, conditions_top, CANVAS_BLACK);
    }
    if (!dashboard->ordered_modules && dashboard->show_tide)
        horizontal_line(&canvas, OUTER_X, OUTER_RIGHT, layout.tide_top, CANVAS_BLACK);
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        const int column_x = day_column_x(day);
        if (day > 0)
            vertical_line(&canvas, column_x, HEADER_BOTTOM,
                          dashboard->show_dedicated_footer
                              ? canvas_footer_top(&canvas)
                              : canvas_outer_bottom(&canvas),
                          CANVAS_BLACK);
        draw_text(&canvas, column_x + 17, DAY_LABEL_BASELINE,
                  WIND_FONT_BERKELEY_MONO_BOLD, WIND_FONT_SIZE_DAY,
                  safe_text(dashboard->days[day].day));
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            if (!dashboard->custom_modules && dashboard->state != WIND_RENDERER_UNAVAILABLE)
                draw_sample(&canvas, forecast_sample_center_x(day, sample),
                            &dashboard->days[day].samples[sample], &layout);
            const int center_x = forecast_sample_center_x(day, sample);
            const wind_renderer_sample_t *slot = &dashboard->days[day].samples[sample];
            if (!dashboard->ordered_modules && dashboard->state != WIND_RENDERER_UNAVAILABLE &&
                dashboard->show_weather)
                draw_weather(&canvas, center_x, layout.weather_center, slot->weather);
            if (!dashboard->ordered_modules && dashboard->state != WIND_RENDERER_UNAVAILABLE &&
                dashboard->show_temperature)
                draw_temperature(&canvas, center_x, layout.temperature_top,
                                 layout.temperature_bottom, slot,
                                 dashboard->temperature_fahrenheit,
                                 dashboard->custom_modules ? module_text_center(layout.temperature_top, 0, dashboard->show_weather != 0) : 0);
        }
    }

    dashboard_layout_t wind_overlay_layout = layout;
    if (dashboard->ordered_modules) draw_ordered_modules(&canvas, dashboard, &wind_overlay_layout);
    else if (dashboard->custom_modules) draw_modules(&canvas, dashboard, &layout, &wind_overlay_layout);

    if (!dashboard->ordered_modules && dashboard->state != WIND_RENDERER_UNAVAILABLE && dashboard->show_tide)
        draw_tide(&canvas, dashboard, &layout);

    if (dashboard->state == WIND_RENDERER_UNAVAILABLE) {
        const char *message = "FORECAST UNAVAILABLE";
        const wind_text_metrics_t metrics = wind_font_measure(
            WIND_FONT_BERKELEY_MONO_BOLD, WIND_FONT_SIZE_DAY, message);
        draw_text(&canvas, (WIND_RENDERER_WIDTH - metrics.width) / 2,
                  299 + (canvas.height - WIND_RENDERER_HEIGHT) / 2,
                  WIND_FONT_BERKELEY_MONO_BOLD, WIND_FONT_SIZE_DAY, message);
    }

    draw_footer(&canvas, dashboard);

    int output_result = 0;
    if (output_format == OUTPUT_PALETTE) {
        output_result = dither_once(canvas.pixels, output_pixels, canvas.height);
    } else if (output_format == OUTPUT_GRAY4) {
        for (size_t pixel = 0; pixel < canvas.size; ++pixel) {
            output_pixels[pixel] =
                (uint8_t)(((unsigned)canvas.pixels[pixel] * 3u + 127u) / 255u);
        }
    } else if (output_format == OUTPUT_GC16) {
        for (size_t pixel = 0; pixel < canvas.size; ++pixel) {
            output_pixels[pixel] =
                (uint8_t)(((unsigned)canvas.pixels[pixel] * 15u + 127u) / 255u);
        }
    } else {
        for (size_t pixel = 0; pixel < canvas.size; ++pixel) {
            const size_t offset = pixel * 4u;
            const uint8_t luma = canvas.pixels[pixel];
            output_pixels[offset] = luma;
            output_pixels[offset + 1] = luma;
            output_pixels[offset + 2] = luma;
            output_pixels[offset + 3] = 255;
        }
    }
    output_surface_t output = {
        .pixels = output_pixels,
        .format = output_format,
    };
    if (output_result == 0 && dashboard->display_mode == WIND_RENDERER_MODE_THRESHOLD &&
        dashboard->state != WIND_RENDERER_UNAVAILABLE) {
        if (!dashboard->custom_modules) draw_threshold_overlay(&canvas, &output, dashboard, &layout);
        else if (dashboard->wind_size == 2) {
            draw_threshold_overlay(&canvas, &output, dashboard, &wind_overlay_layout);
        }
    }
    if (output_result == 0)
        draw_low_battery_overlay(&canvas, &output, dashboard->battery_percent,
                                 dashboard->show_dedicated_footer != 0);
    if (stats) {
        stats->dither_passes =
            output_result == 0 && output_format == OUTPUT_PALETTE ? 1 : 0;
        stats->status_right = CONTENT_RIGHT;
        stats->clipped_primitives = canvas.clipped;
        stats->wind_baseline = layout.wind_baseline;
        stats->weather_row_top = layout.weather_top;
        stats->temperature_row_top = layout.temperature_top;
        stats->tide_row_top = layout.tide_top;
    }
    free(canvas.pixels);
    return output_result == 0 ? 0 : -2;
}

int wind_renderer_render(const wind_renderer_dashboard_t *dashboard,
                         uint8_t *palette_out, size_t palette_size,
                         wind_renderer_stats_t *stats) {
    return render_dashboard(dashboard, palette_out, palette_size, OUTPUT_PALETTE,
                            stats);
}

int wind_renderer_render_for_display(const wind_renderer_dashboard_t *dashboard,
                                     wind_renderer_display_t display,
                                     uint8_t *logical_out, size_t logical_size,
                                     wind_renderer_stats_t *stats) {
    if (display == WIND_RENDERER_DISPLAY_E1001_GRAY4) {
        return render_dashboard(dashboard, logical_out, logical_size, OUTPUT_GRAY4,
                                stats);
    }
    if (display == WIND_RENDERER_DISPLAY_E1002_SPECTRA6) {
        return wind_renderer_render(dashboard, logical_out, logical_size, stats);
    }
    if (display == WIND_RENDERER_DISPLAY_E1003_GC16) {
        return render_dashboard(dashboard, logical_out, logical_size, OUTPUT_GC16,
                                stats);
    }
    return -1;
}

int wind_renderer_display_dimensions(wind_renderer_display_t display, int *width,
                                     int *height) {
    if (!width || !height) return -1;
    if (display == WIND_RENDERER_DISPLAY_E1001_GRAY4 ||
        display == WIND_RENDERER_DISPLAY_E1002_SPECTRA6) {
        *width = WIND_RENDERER_WIDTH;
        *height = WIND_RENDERER_HEIGHT;
        return 0;
    }
    if (display == WIND_RENDERER_DISPLAY_E1003_GC16) {
        *width = WIND_RENDERER_E1003_WIDTH;
        *height = WIND_RENDERER_E1003_HEIGHT;
        return 0;
    }
    return -1;
}

int wind_renderer_project_display_row(wind_renderer_display_t display,
                                      const uint8_t *logical, size_t logical_size,
                                      int target_y, uint8_t *target_row,
                                      size_t target_row_size) {
    int target_width = 0;
    int target_height = 0;
    if (!logical || !target_row ||
        wind_renderer_display_dimensions(display, &target_width, &target_height) != 0 ||
        target_y < 0 || target_y >= target_height ||
        target_row_size < (size_t)target_width) {
        return -1;
    }

    if (display != WIND_RENDERER_DISPLAY_E1003_GC16) {
        if (logical_size < WIND_RENDERER_PALETTE_BYTES) return -1;
        memcpy(target_row, logical + (size_t)target_y * WIND_RENDERER_WIDTH,
               WIND_RENDERER_WIDTH);
        return 0;
    }

    if (logical_size < WIND_RENDERER_E1003_COMPOSITION_BYTES) return -1;
    const int source_y =
        target_y * WIND_RENDERER_E1003_COMPOSITION_HEIGHT / target_height;
    const uint8_t *source_row = logical + (size_t)source_y * WIND_RENDERER_WIDTH;
    int source_x = 0;
    int source_x_remainder = 0;
    for (int target_x = 0; target_x < target_width; ++target_x) {
        target_row[target_x] = source_row[source_x];
        source_x_remainder += WIND_RENDERER_WIDTH;
        if (source_x_remainder >= target_width) {
            ++source_x;
            source_x_remainder -= target_width;
        }
    }
    return 0;
}

uint64_t wind_renderer_display_signature(uint64_t base,
                                         wind_renderer_display_t display) {
    if (display != WIND_RENDERER_DISPLAY_E1001_GRAY4 &&
        display != WIND_RENDERER_DISPLAY_E1002_SPECTRA6 &&
        display != WIND_RENDERER_DISPLAY_E1003_GC16) {
        return 0;
    }
    return base ^ (UINT64_C(0x9E3779B97F4A7C15) * (uint64_t)display);
}

int wind_renderer_render_preview_rgba(const wind_renderer_dashboard_t *dashboard,
                                      uint8_t *rgba_out, size_t rgba_size,
                                      wind_renderer_stats_t *stats) {
    return render_dashboard(dashboard, rgba_out, rgba_size, OUTPUT_RGBA, stats);
}

int wind_renderer_render_preview_rgba_for_display(
    const wind_renderer_dashboard_t *dashboard, wind_renderer_display_t display,
    uint8_t *rgba_out, size_t rgba_size, wind_renderer_stats_t *stats) {
    if (display == WIND_RENDERER_DISPLAY_E1003_GC16) {
        return render_dashboard(dashboard, rgba_out, rgba_size, OUTPUT_RGBA_GC16,
                                stats);
    }
    if (display == WIND_RENDERER_DISPLAY_E1001_GRAY4 ||
        display == WIND_RENDERER_DISPLAY_E1002_SPECTRA6) {
        return wind_renderer_render_preview_rgba(dashboard, rgba_out, rgba_size, stats);
    }
    return -1;
}
