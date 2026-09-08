# Third-party notices

The root [`LICENSE`](LICENSE) applies to source code created for Windpeek.
Third-party components and assets retain their own terms and are not relicensed
by the Windpeek GPL notice.

## Firmware sources

- The firmware is based on `aitjcize/esp32-photoframe` commit `6a4eeac`. Its MIT
  notice is preserved in
  [`firmware/LICENSES/esp32-photoframe-MIT.txt`](firmware/LICENSES/esp32-photoframe-MIT.txt).
- AceTimeC is MIT-licensed; its notice is in
  [`firmware/components/acetimec/LICENSE`](firmware/components/acetimec/LICENSE).
- The bundled XPowersLib sources in `firmware/components/pmic_driver_axp2101/`
  carry their own MIT notices in the source files.

## Design assets and dependencies

- Inter is distributed under the SIL Open Font License 1.1.
- The E1002 product model is derived from Seeed Studio's CAD data and is included
  under direct redistribution permission from Seeed Studio. See
  [`docs/assets.md`](docs/assets.md) and the model's
  [`provenance.json`](web/public/devices/e1002/provenance.json).
- Dependencies installed by ESP-IDF, npm, and other package managers retain
  their own licenses.
- Generated location data retains the rights and attribution recorded in its
  checked-in provenance files.
