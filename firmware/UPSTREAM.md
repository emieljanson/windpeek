# Firmware origin

Based on `aitjcize/esp32-photoframe` commit `6a4eeac` under its MIT license.
The E1002 ED2208 driver includes the locally validated Spectra 6 transport
palette and initialization correction.

## UC8179 Gray4 (experimental E1001 path)

- Upstream repository: `Seeed-Projects/Seeed_GxEPD2`
- Pinned commit: `1100ea37c16b910fd79152f4250c13d802b9c20b`
- Source file: `examples/GxEPD2_reTerminal_E1001_Gray4/GxEPD2_reTerminal_E1001_Gray4.ino`
- Source SHA-256: `89f447a07ae5cfee888d83eb0bccc4e61580d96227ecd5da7329505cae2b509f`
- Upstream license: GNU GPL v3.0 (`LICENSE` at the pinned commit)
- Local port: `components/epaper_driver_uc8179/src/driver_uc8179.c`

The five 42-byte external Gray4 tables (`LUTC`, `LUTWW`, `LUTKW`, `LUTWK`,
and `LUTKK`), the reset/init register values, 2 MHz SPI rate, refresh and
sleep commands, inverted panel polarity, and DTM1-low/DTM2-high plane order
are copied from that exact source. The local file is explicitly marked as a
modified ESP-IDF port under the upstream license provenance.

Because this driver is linked into the universal firmware image, the combined
firmware source and binaries are distributed under GPL-3.0-only. MIT-licensed
parts keep their original notices. See `LICENSING.md` for the project-level
distribution statement and the separate asset caveat.

Local deviations are limited to replacing Arduino SPI/GPIO/delay calls with
ESP-IDF APIs, accepting Windpeek's one-byte-per-logical-pixel 800x480
surface, encoding the two 48,000-byte planes in PSRAM, propagating SPI
errors, and replacing the upstream unbounded BUSY loop with a 40-second
bounded wait. Initialization and all five external LUT writes run again for
every refresh after reset, including wake after panel deep sleep.

This is deliberately an **external-LUT experimental path**. It does not read
OTP and is unreachable until the persisted E1001 profile has selected this
backend. Its electrical behavior, waveform quality, panel revision coverage,
and sleep/wake correctness are not claimed until the U7 physical acceptance
matrix passes. There is no automatic controller probe in production code.
