#ifndef WIND_RENDERER_CANVAS_H
#define WIND_RENDERER_CANVAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wind_font.h"

enum { CANVAS_WHITE = 255, CANVAS_BLACK = 0 };

typedef struct {
    uint8_t *pixels;
    int height;
    size_t size;
    bool native_e1003;
    bool muted_status;
    uint8_t *font_mask;
    int clipped;
    bool antialias_text;
    bool smooth_curves;
    int day_count;
    int sample_count;
} canvas_t;

int wind_canvas_native_x(int x);
int wind_canvas_native_y(int y);
void wind_canvas_native_rect(canvas_t *canvas, int left, int top, int width, int height, uint8_t gray);
uint8_t wind_canvas_get_pixel(const canvas_t *canvas, int x, int y);
void wind_canvas_set_pixel(canvas_t *canvas, int x, int y, uint8_t gray);
void wind_canvas_fill_rect(canvas_t *canvas, int x, int y, int width, int height, uint8_t gray);
void wind_canvas_horizontal_line(canvas_t *canvas, int x0, int x1, int y, uint8_t gray);
void wind_canvas_vertical_line(canvas_t *canvas, int x, int y0, int y1, uint8_t gray);
void wind_canvas_grid_dot(canvas_t *canvas, int x, int y);
void wind_canvas_outline_rect(canvas_t *canvas, int x, int y, int width, int height);
void wind_canvas_draw_text_color(canvas_t *canvas, int x, int baseline,
                     wind_font_family_t family, int size, uint8_t gray, const char *text);
void wind_canvas_draw_text(canvas_t *canvas, int x, int baseline, wind_font_family_t family,
               int size, const char *text);
void wind_canvas_draw_text_mask(canvas_t *canvas, int x, int baseline,
                    wind_font_family_t family, int size, const char *text);
void wind_canvas_fade_region_to_white(canvas_t *canvas, int left, int top, int right, int bottom);
int wind_canvas_rightmost_ink_pixel(const canvas_t *canvas, int left, int top, int right, int bottom);
void wind_canvas_draw_outlined_text_center(canvas_t *canvas, int center_x, int baseline,
                               wind_font_family_t family, int size, const char *text);

#endif
