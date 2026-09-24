#!/usr/bin/env python3
"""Rasterize the bundled Bootstrap Icons SVGs for the E1003 panel."""

from io import BytesIO
from pathlib import Path

import cairosvg
from PIL import Image


ROOT = Path(__file__).parent
ICONS = (
    "sun-fill", "moon-fill", "cloud-sun-fill", "cloud-moon-fill",
    "cloud-fill", "cloud-drizzle-fill", "cloud-rain-fill",
    "cloud-rain-heavy-fill",
)
SIZE = 42  # 18 logical pixels at the E1003's 2.34x panel scale.


def main():
    lines = [
        "#pragma once", "#include <stdint.h>", "",
        "/* Bootstrap Icons (MIT), rasterized from bundled SVG at E1003 resolution. */",
    ]
    for name in ICONS:
        svg = (ROOT / "bootstrap_svg" / f"{name}.svg").read_bytes()
        png = cairosvg.svg2png(bytestring=svg, output_width=SIZE, output_height=SIZE)
        alpha = Image.open(BytesIO(png)).convert("RGBA").getchannel("A").tobytes()
        symbol = "BOOTSTRAP_" + name.removesuffix("-fill").upper().replace("-", "_") + "_42"
        lines.append(f"static const uint8_t {symbol}[{SIZE * SIZE}] = {{")
        for start in range(0, len(alpha), 16):
            lines.append("    " + ", ".join(str(value) for value in alpha[start:start + 16]) + ",")
        lines.extend(("};", ""))
    (ROOT.parent / "bootstrap_weather_icons_native.h").write_text("\n".join(lines))


if __name__ == "__main__":
    main()
