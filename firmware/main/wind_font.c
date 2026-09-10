#include "wind_font.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "fonts/wind_font_internal.h"
static const wind_font_asset_t *find_asset(wind_font_family_t family,
                                           int pixel_size) {
    if (family == WIND_FONT_BERKELEY_MONO_BOLD) {
        if (pixel_size == 15) return &wind_font_berkeley_mono_bold_15;
    } else if (family == WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED) {
        if (pixel_size == 12) return &wind_font_berkeley_mono_bold_condensed_12;
        if (pixel_size == 15) return &wind_font_berkeley_mono_bold_condensed_15;
    } else if (family == WIND_FONT_INTER) {
        if (pixel_size == 43) return &wind_font_inter_43;
    }
    return NULL;
}

static uint32_t next_codepoint(const char **cursor) {
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t cp;
    size_t count;

    if (*s < 0x80) {
        *cursor += 1;
        return *s;
    }
    if ((*s & 0xe0) == 0xc0) {
        cp = *s & 0x1f;
        count = 2;
    } else if ((*s & 0xf0) == 0xe0) {
        cp = *s & 0x0f;
        count = 3;
    } else if ((*s & 0xf8) == 0xf0) {
        cp = *s & 0x07;
        count = 4;
    } else {
        *cursor += 1;
        return '?';
    }

    for (size_t i = 1; i < count; ++i) {
        if ((s[i] & 0xc0) != 0x80) {
            *cursor += 1;
            return '?';
        }
        cp = (cp << 6) | (s[i] & 0x3f);
    }
    if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) ||
        (count == 4 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff)) {
        *cursor += 1;
        return '?';
    }
    *cursor += count;
    return cp;
}

static const wind_font_glyph_t *find_glyph(const wind_font_asset_t *asset,
                                            uint32_t codepoint) {
    size_t lo = 0;
    size_t hi = asset->glyph_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const uint32_t candidate = asset->glyphs[mid].codepoint;
        if (candidate < codepoint)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < asset->glyph_count && asset->glyphs[lo].codepoint == codepoint)
        return &asset->glyphs[lo];
    /* Single-letter Latin approximations, used only when the actual glyph is
     * absent. '?' retains the normal fallback for ligatures and nonletters. */
    static const char latin_fallback[] =
        "AAAAAA?CEEEEIIII" /* U+00C0 */
        "?NOOOOO?OUUUUY??" /* U+00D0 */
        "aaaaaa?ceeeeiiii" /* U+00E0 */
        "?nooooo?ouuuuy?y" /* U+00F0 */
        "AaAaAaCcCcCcCcDd" /* U+0100 */
        "DdEeEeEeEeEeGgGg" /* U+0110 */
        "GgGgHhHhIiIiIiIi" /* U+0120 */
        "Ii??JjKkkLlLlLl?" /* U+0130 */
        "?LlNnNnNn?NnOoOo" /* U+0140 */
        "Oo??RrRrRrSsSsSs" /* U+0150 */
        "SsTtTt??UuUuUuUu" /* U+0160 */
        "UuUuWwYyYZzZzZzs"; /* U+0170 */
    if (codepoint >= 0x00c0 && codepoint <= 0x017f)
        return find_glyph(asset, (uint32_t)latin_fallback[codepoint - 0x00c0]);
    return &asset->glyphs[asset->fallback_index];
}

static int measure_width(const wind_font_asset_t *asset, const char *utf8) {
    int width = 0;
    const char *cursor = utf8 ? utf8 : "";
    while (*cursor) {
        const wind_font_glyph_t *glyph = find_glyph(asset, next_codepoint(&cursor));
        if (width > INT_MAX - glyph->advance) return INT_MAX;
        width += glyph->advance;
    }
    return width;
}

wind_text_metrics_t wind_font_measure(wind_font_family_t family, int pixel_size,
                                      const char *utf8) {
    wind_text_metrics_t result = {0, 0, 0};
    const wind_font_asset_t *asset = find_asset(family, pixel_size);
    if (!asset) return result;
    result.width = measure_width(asset, utf8);
    result.ascent = asset->ascent;
    result.descent = asset->descent;
    return result;
}

static void draw_font(uint8_t *luma, int width, int height, int stride, int x,
                      int baseline_y, wind_font_family_t family, int pixel_size,
                      uint8_t gray, const char *utf8, bool antialiased) {
    const wind_font_asset_t *asset = find_asset(family, pixel_size);
    const char *cursor = utf8 ? utf8 : "";
    if (!luma || !asset || width <= 0 || height <= 0 || stride < width) return;

    while (*cursor) {
        const wind_font_glyph_t *glyph = find_glyph(asset, next_codepoint(&cursor));
        const int left = x + glyph->bearing_x;
        const int top = baseline_y + glyph->bearing_y;
        for (int gy = 0; gy < glyph->bitmap_height; ++gy) {
            const int py = top + gy;
            if (py < 0 || py >= height) continue;
            for (int gx = 0; gx < glyph->bitmap_width; ++gx) {
                const int px = left + gx;
                if (px < 0 || px >= width) continue;
                const uint8_t alpha = asset->bitmap[glyph->bitmap_offset +
                                                    gy * glyph->bitmap_width + gx];
                uint8_t *destination = &luma[py * stride + px];
                if (antialiased) {
                    *destination = (uint8_t)((gray * alpha +
                                              *destination * (255u - alpha) + 127u) /
                                             255u);
                } else if (alpha >= 128) {
                    *destination = gray;
                }
            }
        }
        x += glyph->advance;
    }
}

void wind_font_draw(uint8_t *luma, int width, int height, int stride, int x,
                    int baseline_y, wind_font_family_t family, int pixel_size,
                    uint8_t gray, const char *utf8) {
    draw_font(luma, width, height, stride, x, baseline_y, family, pixel_size,
              gray, utf8, false);
}

void wind_font_draw_antialiased(uint8_t *luma, int width, int height,
                                int stride, int x, int baseline_y,
                                wind_font_family_t family, int pixel_size,
                                uint8_t gray, const char *utf8) {
    draw_font(luma, width, height, stride, x, baseline_y, family, pixel_size,
              gray, utf8, true);
}
