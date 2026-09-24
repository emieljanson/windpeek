#include "wind_renderer_native.h"

#include <math.h>
#include <stddef.h>

static int clamp_int(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static double triangle_edge(double ax, double ay, double bx, double by,
                            double px, double py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void wind_renderer_native_triangle(uint8_t *pixels, double ax, double ay,
                                   double bx, double by, double cx, double cy) {
    const double scale_x = (double)WIND_RENDERER_E1003_WIDTH / WIND_RENDERER_WIDTH;
    const double scale_y = (double)WIND_RENDERER_E1003_HEIGHT /
                           WIND_RENDERER_E1003_COMPOSITION_HEIGHT;
    ax *= scale_x; bx *= scale_x; cx *= scale_x;
    ay *= scale_y; by *= scale_y; cy *= scale_y;
    const int left = clamp_int((int)floor(fmin(fmin(ax, bx), cx)) - 1,
                               0, WIND_RENDERER_E1003_WIDTH - 1);
    const int right = clamp_int((int)ceil(fmax(fmax(ax, bx), cx)) + 1,
                                0, WIND_RENDERER_E1003_WIDTH - 1);
    const int top = clamp_int((int)floor(fmin(fmin(ay, by), cy)) - 1,
                              0, WIND_RENDERER_E1003_HEIGHT - 1);
    const int bottom = clamp_int((int)ceil(fmax(fmax(ay, by), cy)) + 1,
                                 0, WIND_RENDERER_E1003_HEIGHT - 1);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            int covered = 0;
            for (int sy = 0; sy < 2; ++sy)
                for (int sx = 0; sx < 2; ++sx) {
                    const double px = x + (sx ? 0.75 : 0.25);
                    const double py = y + (sy ? 0.75 : 0.25);
                    const double e0 = triangle_edge(ax, ay, bx, by, px, py);
                    const double e1 = triangle_edge(bx, by, cx, cy, px, py);
                    const double e2 = triangle_edge(cx, cy, ax, ay, px, py);
                    covered += (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                               (e0 <= 0 && e1 <= 0 && e2 <= 0);
                }
            if (covered) {
                uint8_t *pixel = pixels + (size_t)y * WIND_RENDERER_E1003_WIDTH + x;
                *pixel = (uint8_t)((*pixel * (4 - covered) + 2) / 4);
            }
        }
}

void wind_renderer_native_curve(uint8_t *pixels, double x0, double y0,
                                double x1, double y1, int width, uint8_t color,
                                int clip_left, int clip_right) {
    x0 *= (double)WIND_RENDERER_E1003_WIDTH / WIND_RENDERER_WIDTH;
    x1 *= (double)WIND_RENDERER_E1003_WIDTH / WIND_RENDERER_WIDTH;
    y0 *= (double)WIND_RENDERER_E1003_HEIGHT / WIND_RENDERER_E1003_COMPOSITION_HEIGHT;
    y1 *= (double)WIND_RENDERER_E1003_HEIGHT / WIND_RENDERER_E1003_COMPOSITION_HEIGHT;
    const double dx = x1 - x0, dy = y1 - y0;
    const double length_squared = dx * dx + dy * dy;
    const double radius = width * (double)WIND_RENDERER_E1003_WIDTH /
                          WIND_RENDERER_WIDTH / 2.0;
    const int left = clamp_int((int)floor(fmin(x0, x1) - radius),
                               clip_left, clip_right);
    const int right = clamp_int((int)ceil(fmax(x0, x1) + radius),
                                clip_left, clip_right);
    const int top = clamp_int((int)floor(fmin(y0, y1) - radius),
                              0, WIND_RENDERER_E1003_HEIGHT - 1);
    const int bottom = clamp_int((int)ceil(fmax(y0, y1) + radius),
                                 0, WIND_RENDERER_E1003_HEIGHT - 1);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            double t = length_squared > 0
                ? ((x - x0) * dx + (y - y0) * dy) / length_squared : 0;
            t = fmin(1.0, fmax(0.0, t));
            const double distance = hypot(x - x0 - t * dx, y - y0 - t * dy);
            const double coverage = fmin(1.0, fmax(0.0, radius + 0.5 - distance));
            uint8_t *pixel = pixels + (size_t)y * WIND_RENDERER_E1003_WIDTH + x;
            const uint8_t gray = color == 255
                ? (uint8_t)lround(*pixel + (255 - *pixel) * coverage)
                : (uint8_t)lround(255 * (1.0 - coverage));
            if (color == 255 || gray < *pixel) *pixel = gray;
        }
}
