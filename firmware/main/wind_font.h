#ifndef WIND_FONT_H
#define WIND_FONT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIND_FONT_BERKELEY_MONO_BOLD,
    WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED,
    WIND_FONT_INTER,
} wind_font_family_t;

typedef struct {
    int width;
    int ascent;
    int descent;
} wind_text_metrics_t;

enum {
    WIND_FONT_SIZE_FOOTER = 12,
    WIND_FONT_SIZE_STATUS = 15,
    WIND_FONT_SIZE_DAY = 15,
    WIND_FONT_SIZE_SPOT = 43,
    WIND_FONT_SIZE_OVERVIEW_NATIVE = 70,
};

/* Match a logical dashboard font to the E1003's native font asset. */
int wind_font_native_size(wind_font_family_t family, int logical_size);

wind_text_metrics_t wind_font_measure(wind_font_family_t family, int pixel_size,
                                      const char *utf8);

void wind_font_draw(uint8_t *luma, int width, int height, int stride, int x,
                    int baseline_y, wind_font_family_t family, int pixel_size,
                    uint8_t gray, const char *utf8);

void wind_font_draw_antialiased(uint8_t *luma, int width, int height,
                                int stride, int x, int baseline_y,
                                wind_font_family_t family, int pixel_size,
                                uint8_t gray, const char *utf8);

#ifdef __cplusplus
}
#endif

#endif
