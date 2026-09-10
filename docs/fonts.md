# Wind dashboard fonts

The wind dashboard embeds grayscale glyph bitmaps, not runtime font files. This
keeps text measurement and drawing identical on the host and ESP32 and limits
the firmware cost to the characters and sizes used by the 800 x 480 layout.

## Embedded roles

| Family | Pixel sizes | Dashboard roles |
| --- | --- | --- |
| Berkeley Mono Bold | 15 | Model, update and day labels |
| Berkeley Mono Bold Condensed | 12, 15 | Footer labels; wind, threshold, temperature and tide values |
| Inter Black | 43 | Uppercase spot name |

Each size contains printable ASCII, `°`, `…`, and common Western European
accented characters used in Dutch place names. Missing accented Latin glyphs
in U+00C0–U+017F fall back to a single ASCII letter where possible (for example,
`Kuźnica` becomes `Kuznica`); existing glyphs retain their accents. Other unsupported
characters, including ligatures without a single-letter approximation, and malformed
UTF-8 use the generated `?` glyph. Unsupported family/size pairs
measure as zero and draw nothing; the renderer should use the named constants
in `wind_font.h`.

`wind_font_draw` takes a baseline Y coordinate. Glyph bearings, ascent, descent,
and advances are generated from the source face, and drawing is clipped to the
provided 8-bit luminance buffer. Coverage is alpha-blended toward `gray`; no
dithering is performed in this layer.

## Sources and licensing

Berkeley Mono was generated from the user-supplied licensed source:

`/Users/emieljanson/Library/Fonts/Berkeley Mono Variable.ttf`

SHA-256: `d158608a6095b5bb32980c5a421282831a4c6df5b9979f2cea7916e1c32f3d08`

Berkeley Graphics' licence controls redistribution of Berkeley Mono and derived
glyph assets. These generated files are suitable for this licensed development
workspace; do not publish or redistribute them until the product's font licence
has been checked for that distribution.

Inter was generated from the installed Inter Variable font:

`/Users/emieljanson/Library/Fonts/InterVariable.ttf`

SHA-256: `4989b125924991b90d05b2d16e0e388c48f7d5bb8b30539bbf9c755278d0ccaf`

Inter is distributed under the SIL Open Font License 1.1. If the Inter source is
not available, `generate_fonts.py` deterministically uses the required Berkeley
Mono source for the Inter asset slots and labels that fallback in each generated
file. That fallback is for reproducible development only and remains subject to
the Berkeley Mono licence.

## Reproduction

Assets were produced with Pillow 12.2.0, its bundled FreeType rasterizer, the
Regular named variation, grayscale masks, integer rounded glyph advances, and
no hinting or post-processing beyond Pillow's normal FreeType rendering:

```sh
python3 firmware/main/fonts/generate_fonts.py \
  --berkeley "/Users/emieljanson/Library/Fonts/Berkeley Mono Variable.ttf" \
  --inter "/Users/emieljanson/Library/Fonts/InterVariable.ttf"
```

The generated source files and `wind_font.c` must be added to the firmware and
host-test build lists by the integration unit. Build-list files are intentionally
outside this unit's ownership.
