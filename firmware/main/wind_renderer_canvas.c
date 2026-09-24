#include "wind_renderer_canvas.h"
#include "wind_renderer.h"

#include <string.h>

static const char *safe_text(const char *text) { return text ? text : ""; }
static int clamp_int(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

int wind_canvas_native_x(int x) {
    return (x * WIND_RENDERER_E1003_WIDTH + WIND_RENDERER_WIDTH / 2) /
           WIND_RENDERER_WIDTH;
}

int wind_canvas_native_y(int y) {
    return (y * WIND_RENDERER_E1003_HEIGHT +
            WIND_RENDERER_E1003_COMPOSITION_HEIGHT / 2) /
           WIND_RENDERER_E1003_COMPOSITION_HEIGHT;
}

void wind_canvas_native_rect(canvas_t *canvas, int left, int top, int width,
                        int height, uint8_t gray) {
    if (width == 0 || height == 0) return;
    if (width < 0 || height < 0 || left < 0 || top < 0 ||
        left + width > WIND_RENDERER_E1003_WIDTH ||
        top + height > WIND_RENDERER_E1003_HEIGHT) {
        canvas->clipped++;
        return;
    }
    for (int row = top; row < top + height; ++row)
        memset(canvas->pixels + (size_t)row * WIND_RENDERER_E1003_WIDTH + left,
               gray, (size_t)width);
}

uint8_t wind_canvas_get_pixel(const canvas_t *canvas, int x, int y) {
    if (canvas->native_e1003)
        return canvas->pixels[(size_t)wind_canvas_native_y(y) * WIND_RENDERER_E1003_WIDTH + wind_canvas_native_x(x)];
    return canvas->pixels[y * WIND_RENDERER_WIDTH + x];
}

void wind_canvas_set_pixel(canvas_t *canvas, int x, int y, uint8_t gray) {
    if (x < 0 || x >= WIND_RENDERER_WIDTH || y < 0 || y >= canvas->height) {
        canvas->clipped++;
        return;
    }
    if (canvas->native_e1003) {
        const int left = wind_canvas_native_x(x), top = wind_canvas_native_y(y);
        wind_canvas_native_rect(canvas, left, top, wind_canvas_native_x(x + 1) - left,
                    wind_canvas_native_y(y + 1) - top, gray);
        return;
    }
    canvas->pixels[y * WIND_RENDERER_WIDTH + x] = gray;
}

void wind_canvas_fill_rect(canvas_t *canvas, int x, int y, int width, int height,
                      uint8_t gray) {
    if (width <= 0 || height <= 0) return;
    if (x < 0 || y < 0 || x + width > WIND_RENDERER_WIDTH ||
        y + height > canvas->height) {
        canvas->clipped++;
        return;
    }
    if (canvas->native_e1003) {
        wind_canvas_native_rect(canvas, wind_canvas_native_x(x), wind_canvas_native_y(y),
                    wind_canvas_native_x(x + width) - wind_canvas_native_x(x),
                    wind_canvas_native_y(y + height) - wind_canvas_native_y(y), gray);
        return;
    }
    for (int row = y; row < y + height; ++row)
        memset(canvas->pixels + row * WIND_RENDERER_WIDTH + x, gray, (size_t)width);
}

void wind_canvas_horizontal_line(canvas_t *canvas, int x0, int x1, int y, uint8_t gray) {
    if (x1 < x0) {
        const int swap = x0;
        x0 = x1;
        x1 = swap;
    }
    if (canvas->native_e1003) {
        wind_canvas_native_rect(canvas, wind_canvas_native_x(x0), wind_canvas_native_y(y),
                    wind_canvas_native_x(x1 + 1) - wind_canvas_native_x(x0), 2, gray);
        return;
    }
    wind_canvas_fill_rect(canvas, x0, y, x1 - x0 + 1, 1, gray);
}

void wind_canvas_vertical_line(canvas_t *canvas, int x, int y0, int y1, uint8_t gray) {
    if (y1 < y0) {
        const int swap = y0;
        y0 = y1;
        y1 = swap;
    }
    if (canvas->native_e1003) {
        wind_canvas_native_rect(canvas, wind_canvas_native_x(x), wind_canvas_native_y(y0), 2,
                    wind_canvas_native_y(y1 + 1) - wind_canvas_native_y(y0), gray);
        return;
    }
    for (int y = y0; y <= y1; ++y) wind_canvas_set_pixel(canvas, x, y, gray);
}

void wind_canvas_grid_dot(canvas_t *canvas, int x, int y) {
    if (canvas->native_e1003)
        wind_canvas_native_rect(canvas, wind_canvas_native_x(x), wind_canvas_native_y(y), 2, 2, CANVAS_BLACK);
    else wind_canvas_set_pixel(canvas, x, y, CANVAS_BLACK);
}

void wind_canvas_outline_rect(canvas_t *canvas, int x, int y, int width, int height) {
    wind_canvas_horizontal_line(canvas, x, x + width - 1, y, CANVAS_BLACK);
    wind_canvas_horizontal_line(canvas, x, x + width - 1, y + height - 1, CANVAS_BLACK);
    wind_canvas_vertical_line(canvas, x, y, y + height - 1, CANVAS_BLACK);
    wind_canvas_vertical_line(canvas, x + width - 1, y, y + height - 1, CANVAS_BLACK);
}

void wind_canvas_draw_text(canvas_t *canvas, int x, int baseline, wind_font_family_t family,
                      int size, const char *text) {
    wind_canvas_draw_text_color(canvas, x, baseline, family, size, CANVAS_BLACK, text);
}

void wind_canvas_draw_text_mask(canvas_t *canvas, int x, int baseline,
                           wind_font_family_t family, int size, const char *text) {
    if (canvas->native_e1003) {
        wind_canvas_draw_text_color(canvas, x, baseline, family, size, CANVAS_BLACK, text);
        return;
    }
    wind_font_draw(canvas->pixels, WIND_RENDERER_WIDTH, canvas->height,
                   WIND_RENDERER_WIDTH, x, baseline, family, size, CANVAS_BLACK,
                   safe_text(text));
}

void wind_canvas_fade_region_to_white(canvas_t *canvas, int left, int top, int right,
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
            const uint8_t previous = wind_canvas_get_pixel(canvas, x, y);
            wind_canvas_set_pixel(canvas, x, y, (uint8_t)((int)previous +
                      ((CANVAS_WHITE - (int)previous) * fade + 127) / 255));
        }
    }
}

int wind_canvas_rightmost_ink_pixel(const canvas_t *canvas, int left, int top, int right,
                               int bottom) {
    left = clamp_int(left, 0, WIND_RENDERER_WIDTH - 1);
    right = clamp_int(right, 0, WIND_RENDERER_WIDTH - 1);
    top = clamp_int(top, 0, canvas->height - 1);
    bottom = clamp_int(bottom, 0, canvas->height - 1);
    for (int x = right; x >= left; --x) {
        for (int y = top; y <= bottom; ++y) {
            if (wind_canvas_get_pixel(canvas, x, y) < CANVAS_WHITE) return x;
        }
    }
    return left;
}

void wind_canvas_draw_text_color(canvas_t *canvas, int x, int baseline,
                            wind_font_family_t family, int size, uint8_t gray,
                            const char *text) {
    if (canvas->native_e1003) {
        if (!canvas->font_mask) return;
        enum { MASK_HEIGHT = 80, MASK_BASELINE = 60 };
        memset(canvas->font_mask, CANVAS_WHITE,
               WIND_RENDERER_WIDTH * MASK_HEIGHT);
        if (canvas->antialias_text)
            wind_font_draw_antialiased(canvas->font_mask, WIND_RENDERER_WIDTH,
                                       MASK_HEIGHT, WIND_RENDERER_WIDTH, 0,
                                       MASK_BASELINE, family, size, CANVAS_BLACK,
                                       safe_text(text));
        else
            wind_font_draw(canvas->font_mask, WIND_RENDERER_WIDTH,
                           MASK_HEIGHT, WIND_RENDERER_WIDTH, 0,
                           MASK_BASELINE, family, size, CANVAS_BLACK,
                           safe_text(text));
        const int right = clamp_int(wind_font_measure(family, size,
                                                      safe_text(text)).width + 2,
                                    0, WIND_RENDERER_WIDTH);
        for (int sy = 0; sy < MASK_HEIGHT; ++sy) {
            const int logical_y = baseline - MASK_BASELINE + sy;
            if (logical_y < 0 || logical_y >= canvas->height) continue;
            const int top = wind_canvas_native_y(logical_y);
            const int bottom = wind_canvas_native_y(logical_y + 1);
            for (int sx = 0; sx < right; ++sx) {
                const int logical_x = x + sx;
                if (logical_x < 0 || logical_x >= WIND_RENDERER_WIDTH) continue;
                const unsigned alpha = 255u - canvas->font_mask[sy * WIND_RENDERER_WIDTH + sx];
                if (!alpha) continue;
                const int left = wind_canvas_native_x(logical_x);
                const int pixel_right = wind_canvas_native_x(logical_x + 1);
                for (int py = top; py < bottom; ++py) {
                    uint8_t *pixel = canvas->pixels +
                        (size_t)py * WIND_RENDERER_E1003_WIDTH + left;
                    for (int px = left; px < pixel_right; ++px, ++pixel)
                        *pixel = (uint8_t)((gray * alpha +
                            *pixel * (255u - alpha) + 127u) / 255u);
                }
            }
        }
        return;
    }
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

void wind_canvas_draw_outlined_text_center(canvas_t *canvas, int center_x, int baseline,
                                      wind_font_family_t family, int size,
                                      const char *text) {
    const wind_text_metrics_t metrics =
        wind_font_measure(family, size, safe_text(text));
    const int x = center_x - metrics.width / 2;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if ((dx == 0 && dy == 0) || dx * dx + dy * dy > 4) continue;
            wind_canvas_draw_text_color(canvas, x + dx, baseline + dy, family, size, CANVAS_WHITE,
                            text);
        }
    }
    wind_canvas_draw_text(canvas, x, baseline, family, size, text);
}
