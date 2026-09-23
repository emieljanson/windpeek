#!/usr/bin/env python3
"""Build the E1003 app in an isolated local directory for USB updates."""

import os
import sys
import tempfile
from pathlib import Path

FIRMWARE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FIRMWARE_DIR))
from build import run_idf  # noqa: E402


def main():
    build_dir = Path(tempfile.gettempdir()) / "windpeek-e1003-local-build"
    sdkconfig = Path(tempfile.gettempdir()) / "windpeek-e1003-local-sdkconfig"
    os.chdir(FIRMWARE_DIR)
    run_idf([
        "-B", str(build_dir),
        f"-DSDKCONFIG={sdkconfig}",
        "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;boards/sdkconfig.defaults.seeedstudio_reterminal_e1003",
        "build",
    ])
    binary = build_dir / "windpeek.bin"
    if not binary.is_file():
        raise FileNotFoundError(f"Build completed without {binary}")
    print(f"Local E1003 app ready: {binary}", flush=True)


if __name__ == "__main__":
    main()
