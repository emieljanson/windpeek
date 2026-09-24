# Windpeek firmware

Windpeek is an 800 x 480 wind forecast dashboard for the Seeed Studio
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

Windpeek uses Open-Meteo directly for wind, weather, temperature and optional
sea-level forecasts. The website installs the spot, timezone and forecast model;
every firmware build uses the same fixed Open-Meteo endpoints.

During USB setup the browser seeds both the device clock and its battery-backed
RTC. Local forecast and wake times use the installed spot's IANA timezone via
the bundled TZDB 2025b rules, including daylight-saving changes and fractional
UTC offsets.

## Empty-battery reserve

On supported Windpeek boards, boot and existing scheduled/button work check
battery voltage before starting network work. There is no battery polling task
and no additional timer wake. At or below 3450 mV, the device attempts one full
black `Battery empty` screen, then deep-sleeps without forecast timer wakes.
The attempt is latched in RTC memory and NVS before refreshing, so a reset or
failed refresh does not repeatedly spend the remaining battery. The panel cache
is invalidated so the forecast is redrawn on recovery.

USB power permits normal operation on the next wake; otherwise recovery needs
at least 3650 mV to avoid restarting on voltage rebound. After charging, press a
wake button if attaching USB did not wake the board. Held buttons are excluded
from critical-sleep wake sources to avoid repeated boots.

These voltage thresholds are provisional engineering reserves, not a calibrated
remaining-capacity guarantee. Validate the last refresh under load on each board,
with aged batteries and low temperatures. A sudden power loss can still prevent
the final refresh; a failed attempt is not retried until recovery. The UI has
native black/white encoding for E1001, E1002, and E1003.

## Build and test

```sh
make test
./build.py --board seeedstudio_reterminal_e1002
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

The default build targets the universal E1001/E1002 firmware. E1002 and E1003
remain selectable with `--board`. The old photo-frame application and its
boards are no longer build targets.

### Regenerate E1003 raster assets

The committed font files and weather icon header are generated. Use the font
sources named by the SHA-256 comments in the generated files; the licensed
Berkeley Mono font is supplied separately. From the repository root:

```sh
brew install cairo # macOS; Linux needs libcairo2
python3 -m venv .venv-assets
. .venv-assets/bin/activate
python -m pip install pillow fonttools cairosvg
export DYLD_FALLBACK_LIBRARY_PATH="$(brew --prefix cairo)/lib" # macOS
python firmware/main/fonts/generate_fonts.py --berkeley /path/to/BerkeleyMonoVariable.ttf --inter /path/to/InterVariable.ttf
python firmware/main/fonts/generate_native_weather_icons.py
python firmware/main/fonts/generate_native_weather_icons.py --check
cd web && npm run renderer:build && npm run renderer:check
```

The regular font command regenerates both shared and E1003 sizes.
`--native-e1003-only` regenerates only the added panel-size fonts. The icon
header records a hash of the bundled SVGs and render size.

### Local E1003 USB update

From `web/`, run `npm run device:e1003`, then open
`http://127.0.0.1:4186/dev/e1003-flash.html` in Dia and press
**Connect and flash E1003**. The command builds the current E1003 firmware,
serves it only on localhost, and the page flashes only the application. Saved
Wi-Fi and spot configuration stay on the device. Keep the command running until
the page says the device is restarting.

The E1002 build contains only the Windpeek dashboard, USB installer, Wi-Fi
client, forecast cache and battery/deep-sleep runtime. The upstream photo-frame
UI, albums, captive portal, Home Assistant and photo OTA runtime are excluded.
See `UPSTREAM.md` for origin and license details.

## Licensing

The combined Windpeek firmware source that includes the UC8179 E1001 driver is
distributed under GNU GPL v3.0 only. Existing MIT-licensed portions retain their
MIT notices. See `LICENSING.md` for the exact boundary and third-party assets
that require separate distribution rights.

### E1003 spot overview

Tap the spot name or press the green button to open the fullscreen overview.
It shows up to three configured spots with five forecast days, using each spot's
large wind or swell graph (configured module order breaks ties). Tap a row to
open that spot. Swipe up/down or use the lower-right chevrons to move between
pages. The two other physical buttons select the previous/next spot directly.
They do this from the overview too. Each accepted screen navigation gives a
short buzzer click before the e-ink refresh begins.
Additional presses during that refresh are ignored; release a held button
before pressing it again.
The chevrons have separate 46×46 logical touch targets around 34×34 artwork.

The GT911 driver uses the existing I2C bus. Touch can wake the E1003; the current
overview page survives deep sleep and resets when its configuration changes.
On battery, use two separate taps within about 1.5 seconds to wake by touch.
That is the GT911's longest double-tap interval; the physical buttons also wake
the device and give an immediate click while the e-ink image catches up.
Rendering and fetching suppress idle sleep. Cached forecasts remain usable
offline; missing forecast samples render as unavailable.

Host tests cover gesture cancellation, page boundaries, hidden targets and
mixed wind/swell rendering. `output/spot-overview/render-production.c` exports
the production renderer at 1872×1404. Physical touch orientation, tap/swipe
response and wake behavior still require verification on an attached E1003.
