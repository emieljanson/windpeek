#include <stdio.h>
#include "wind_renderer.h"

/* Export the device palette through the same RGB conversion used for streaming. */
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    uint8_t palette[WIND_RENDERER_PALETTE_BYTES];
    uint8_t row[WIND_RENDERER_WIDTH * 3];
    if (wind_renderer_render_battery_empty(palette, sizeof(palette)) != 0) return 1;
    FILE *output = fopen(argv[1], "wb");
    if (!output) return 1;
    fprintf(output, "P6\n%d %d\n255\n", WIND_RENDERER_WIDTH, WIND_RENDERER_HEIGHT);
    for (int y = 0; y < WIND_RENDERER_HEIGHT; ++y) {
        if (wind_renderer_palette_row_to_rgb(palette + y * WIND_RENDERER_WIDTH,
                WIND_RENDERER_WIDTH, row, sizeof(row)) != 0 ||
            fwrite(row, 1, sizeof(row), output) != sizeof(row)) {
            fclose(output);
            return 1;
        }
    }
    return fclose(output) != 0;
}
