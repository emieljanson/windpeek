#ifndef WIND_FONT_INTERNAL_H
#define WIND_FONT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    uint32_t bitmap_offset;
    uint16_t bitmap_width;
    uint16_t bitmap_height;
    int16_t bearing_x;
    int16_t bearing_y;
    uint16_t advance;
} wind_font_glyph_t;

typedef struct {
    int pixel_size;
    int ascent;
    int descent;
    const wind_font_glyph_t *glyphs;
    size_t glyph_count;
    size_t fallback_index;
    const uint8_t *bitmap;
} wind_font_asset_t;

extern const wind_font_asset_t wind_font_berkeley_mono_bold_15;
extern const wind_font_asset_t wind_font_berkeley_mono_bold_condensed_12;
extern const wind_font_asset_t wind_font_berkeley_mono_bold_condensed_15;
extern const wind_font_asset_t wind_font_inter_43;
extern const wind_font_asset_t wind_font_berkeley_mono_bold_35;
extern const wind_font_asset_t wind_font_berkeley_mono_bold_80;
extern const wind_font_asset_t wind_font_berkeley_mono_bold_condensed_28;
extern const wind_font_asset_t wind_font_berkeley_mono_bold_condensed_35;
extern const wind_font_asset_t wind_font_inter_101;
extern const wind_font_asset_t wind_font_inter_70;

#endif
