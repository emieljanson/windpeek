#ifndef WIND_RENDERER_OUTPUT_H
#define WIND_RENDERER_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OUTPUT_PALETTE,
    OUTPUT_GRAY4,
    OUTPUT_GC16,
    OUTPUT_RGBA,
    OUTPUT_RGBA_GC16,
    OUTPUT_RGBA_GRAY4,
} output_format_t;

// Encode a completed luminance canvas for the selected panel or preview.
int wind_renderer_encode_luma(const uint8_t *luma, size_t count, int height,
                             uint8_t *output, output_format_t format);

#endif
