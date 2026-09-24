# Windpeek

This is the Windpeek monorepo. The production configurator, browser installer,
device firmware, shared renderer, tests, and release workflow all live here.
The former `windscout-site` repository is legacy and is not a build or release
dependency.

## Browser installer artifacts

Owner instructions are in [`docs/setup.md`](docs/setup.md), USB recovery in
[`docs/recovery.md`](docs/recovery.md), and release evidence in
[`docs/release.md`](docs/release.md).

Every push to `main` builds the configurator and deploys it with the universal
E1001/E1002 and E1003 firmware bundles. Firmware builds are reused when their
source and build inputs match the production cache, retaining the original
firmware version. Cache misses rebuild both targets for the current commit.
Tagged releases (`v*`) additionally publish the application and browser-installer
files as a GitHub Release. The bundle is generated from ESP-IDF's
`flasher_args.json`, contains separate checksummed flash parts, and exposes
clean-install and preserving-update write sets for client compatibility. The
current browser installer always uses the clean-install set when flashing.
Generate and validate the bundle locally with one command. When no version is supplied, this creates a fresh
`dev-local-…` version and embeds that same value in the device firmware:

```sh
cd firmware
./build.py --board seeedstudio_reterminal_e100x \
  --installer-output ../web/public/firmware
```

Do not publish a preserving update unless the generator confirms that its ranges avoid NVS and storage.
The local installer refuses to reuse a version when its firmware bytes differ,
so a stale dashboard can never be installed silently.

Starting the web configurator with `npm run dev` prepares this local installer
bundle automatically when a firmware build is available. You can also run it
explicitly with `npm run installer:prepare` from `web/`.

Windpeek turns a Seeed Studio reTerminal E1002 into a quiet, battery-efficient
wind forecast dashboard. The device downloads forecast data itself and renders
the complete interface locally; no map service or rendering backend is
required. The browser configurator compiles that same renderer to WebAssembly.
Both surfaces therefore share one 800 x 480 composition and all drawing logic;
only the final output pass differs. The device applies the exact e-ink palette
and dithering, while the browser keeps the pre-dither grayscale and red accents
for a clean, sharp preview.

## Anonymous dashboard activity

Production firmware attempts an anonymous PostHog heartbeat after it has
successfully downloaded and stored a new forecast. Failed deliveries retry
after the next successful forecast refresh; after a successful delivery,
the device waits seven days before sending again. The heartbeat
contains a random dashboard ID, firmware version and device type. It contains
no location, Wi-Fi details, configuration or weather data. The random ID stays
on the device until its storage is fully erased or it is factory-flashed.

The active implementation and setup instructions live in
[`firmware/`](firmware/README.md). Product decisions and acceptance criteria are
captured in [`docs/plans/2026-08-24-1109-feat-local-wind-dashboard-plan.md`](docs/plans/2026-08-24-1109-feat-local-wind-dashboard-plan.md).

## Repository layout

- `firmware/main/`: forecast, cache, schedule, renderer and device integration.
- `firmware/host_tests/`: deterministic host tests and 800 x 480 golden frames.
- `web/`: interactive 3D configurator with direct browser forecast retrieval.
- `shared/renderer-fixtures/`: full-palette parity fixtures shared by native and
  WebAssembly tests.
- `docs/`: font provenance, implementation plan and durable rendering lessons.

## Web configurator

Run the configurator locally from `web/`:

```sh
npm install
npm run dev
```

The checked-in WebAssembly module is generated with a pinned Emscripten image.
Regenerate it after changing the renderer, fonts, icons or bridge:

```sh
npm run renderer:build
npm run renderer:check
npm test -- --run tests/shared-renderer.test.js
```

The configurator retrieves Best fit, KNMI, ECMWF, ICON, and GFS in one
Open-Meteo request. Best fit is the default, while the Model control can
switch the 3D preview instantly between the already normalized forecasts.
Cached forecasts are separated by spot and model.

### Refreshing the spot catalog

The shipped location catalog is generated locally from pinned source snapshots. It never queries a spot provider while a user searches.

```sh
cd web
npm run spots:import
npm run spots:validate
npm run spots:review
npm run spots:catalog
npm run spots:catalog:check
```

`spots:validate` reads `VITE_GEOAPIFY_API_KEY` from the environment or `web/.env.local`, calculates the complete cold-run credit cost before making requests, and saves every successful response in the compact validation cache. An unchanged warm run makes no Geoapify requests. Source rights and snapshot identifiers are release gates; inspection-only records cannot enter generated output.

Weather is shown by default. Air temperature and tide are independent optional
rows; wind always remains visible and expands into the space left over. Tide is
retrieved separately from Open-Meteo Marine only when needed on the device, so
a marine-data failure cannot break the regular forecast. Tide timing is an
indicative model forecast and is not suitable for navigation.

The parity tests compare every one of the 384,000 device-palette bytes for all
eight row combinations and a missing-data state, including red threshold
pixels. Compiling to WebAssembly therefore cannot silently change what the
device would receive. A separate preview test verifies that the same
composition can be exported as opaque RGBA with continuous gray and red
accents. Forecast and optional marine requests go directly from the browser to
Open-Meteo; the renderer itself performs no networking or persistence.

The Berkeley Mono bitmap assets are derived from a separately licensed local
font. Check that licence before distributing firmware binaries.

## Firmware updates

Reconnect the device over USB-C and open the browser installer. It chooses a
configuration-only update or a clean firmware installation based on the connected
device and release. Firmware updates erase stored settings and Wi-Fi; enter Wi-Fi
again to finish setup. With current firmware, setup keeps Wi-Fi and actively
fetches, renders and saves a forecast before reporting success, even when the
settings already match.

The upstream photo-frame application has been removed. Firmware builds only
Windpeek for E1001/E1002 and E1003.
