#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "wind_renderer.h"

/* Shape points use 800x600 composition coordinates. Curve clip bounds use
 * physical E1003 x coordinates and are clamped to the panel. */
void wind_renderer_native_triangle(uint8_t *pixels, double ax, double ay,
                                   double bx, double by, double cx, double cy);
void wind_renderer_native_curve(uint8_t *pixels, double x0, double y0,
                                double x1, double y1, int width, uint8_t color,
                                int clip_left, int clip_right);
