#include "wind_renderer_output.h"
#include "wind_renderer.h"

#include <stdlib.h>
#include <string.h>

static int dither_palette(const uint8_t *luma, uint8_t *palette, int height) {
    int *current = calloc((size_t)WIND_RENDERER_WIDTH + 2u, sizeof(int));
    int *next = calloc((size_t)WIND_RENDERER_WIDTH + 2u, sizeof(int));
    if (!current || !next) {
        free(current);
        free(next);
        return -1;
    }
    for (int y = 0; y < height; ++y) {
        memset(next, 0, ((size_t)WIND_RENDERER_WIDTH + 2u) * sizeof(int));
        for (int x = 0; x < WIND_RENDERER_WIDTH; ++x) {
            const int index = y * WIND_RENDERER_WIDTH + x;
            const int diffusion = current[x + 1];
            int adjusted = (int)luma[index] + (diffusion >= 0
                ? (diffusion + 8) / 16 : -((-diffusion + 8) / 16));
            if (adjusted < 0) adjusted = 0;
            if (adjusted > 255) adjusted = 255;
            const int target = adjusted < 128 ? 0 : 255;
            const int error = adjusted - target;
            palette[index] = target == 0 ? 0 : 1;
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

int wind_renderer_encode_luma(const uint8_t *luma, size_t count, int height,
                             uint8_t *output, output_format_t format) {
    if (format == OUTPUT_PALETTE) return dither_palette(luma, output, height);
    if (format == OUTPUT_GRAY4 || format == OUTPUT_GC16) {
        const unsigned levels = format == OUTPUT_GRAY4 ? 3u : 15u;
        for (size_t pixel = 0; pixel < count; ++pixel)
            output[pixel] = (uint8_t)(((unsigned)luma[pixel] * levels + 127u) / 255u);
        return 0;
    }
    for (size_t pixel = 0; pixel < count; ++pixel) {
        const size_t offset = pixel * 4u;
        const uint8_t value = luma[pixel];
        output[offset] = value;
        output[offset + 1] = value;
        output[offset + 2] = value;
        output[offset + 3] = 255;
    }
    return 0;
}
