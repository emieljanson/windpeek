# WindScout firmware

WindScout is an 800 x 480 wind forecast dashboard for the Seeed Studio
reTerminal E1001 and E1002. It fetches a five-day forecast directly, caches the
last valid result, renders locally and only refreshes the E-ink panel when the
final bitmap changed.

## Dashboard

- Five days with samples at 08:00, 11:00, 14:00, 17:00 and 20:00 local time.
- Vertical bars show sustained wind; horizontal markers show gusts.
- Arrows point toward the direction the wind travels.
- A fixed 0-40 kt scale makes days directly comparable.
- Fresh, aged, stale and unavailable states remain legible without color.
- Geometry sits on integer pixels; text and arrows pass through one final
  Floyd-Steinberg monochrome dither pass.

## Forecast service

WindScout uses Open-Meteo directly for wind, weather, temperature and optional
sea-level forecasts. The website installs the spot, timezone and forecast model;
every firmware build uses the same fixed Open-Meteo endpoints.

During USB setup the browser seeds both the device clock and its battery-backed
RTC. Local forecast and wake times use the installed spot's IANA timezone via
the bundled TZDB 2025b rules, including daylight-saving changes and fractional
UTC offsets.

## Build and test

```sh
make test
./build.py --board seeedstudio_reterminal_e1002
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

E1002, universal E1001/E1002 and E1003 builds run only the firmware step by
default. Other boards still build the photo-frame webapp and setup screens.
Use `--step` to request individual build steps explicitly.

The E1002 build contains only the WindScout dashboard, USB installer, Wi-Fi
client, forecast cache and battery/deep-sleep runtime. The upstream photo-frame
UI, albums, captive portal, Home Assistant and photo OTA runtime are excluded.
See `UPSTREAM.md` for origin and license details.

## Licensing

The combined WindScout firmware source that includes the UC8179 E1001 driver is
distributed under GNU GPL v3.0 only. Existing MIT-licensed portions retain their
MIT notices. See `LICENSING.md` for the exact boundary and third-party assets
that require separate distribution rights.
