# Installer resilience — 2026-09-20

## Scope

Shared E1001/E1002/E1003 installer, current working tree. Existing multi-spot,
display and installer changes were retained. These observations do not establish
the cause of every failure on older released firmware.

## Reproduced failures

- `get_state` crashed the UART task with a stack overflow. The command dispatcher
  reserved two full multi-spot configurations on its stack, even for small status
  requests. Candidate parsing now allocates these temporary records on the heap.
- Applying after a successful Wi-Fi test returned `commit_failed` before any
  forecast fetch. Instrumentation confirmed `ESP_ERR_NO_MEM` from creating the
  apply task; the 45-byte acknowledgement had transmitted successfully. The
  approximately 60 KiB installer state (mainly USB buffers) now lives in PSRAM,
  leaving internal RAM for task stacks and networking. The apply worker is
  reserved at boot and reused; delayed setup and retries no longer need a fresh
  contiguous 32 KiB stack allocation.
- The USB session expired after two minutes without commands, allowing automatic
  light sleep to silence the UART. USB-powered sessions now stay available, also
  before the first handshake and after successful apply.
- A successful draw of “Forecast unavailable” could satisfy render verification.
  Setup now requires a successful fetch, validated coverage and confirmed panel
  output. Failed setup fetches do not replace the panel or commit the candidate.
- Reusing `sdkconfig` from another board overrode the requested build defaults.
  Builds now reject that mismatch before building/flashing and require fullclean.

## Automated regression coverage

- Five hours of simulated waiting at Wi-Fi; current browser time at submission.
  This browser scenario runs for E1001, E1002 and E1003.
- Five hours of offline firmware operation followed by immediate setup fetch and
  immediate retry, independent of scheduled refresh timing.
- Failed fetch with and without existing cache; no setup display or false success.
- Failed panel confirmation and an unchanged, already-confirmed valid forecast.
- USB inactivity versus expired battery-only sessions and credential cleanup.
- Numeric firmware diagnostics survive both browser privacy filters; unexpected
  fields, locations, network names and passwords do not.
- Default configuration alone is not completed setup; saved open Wi-Fi is valid.
- Wrong-board build configuration is rejected.
- E1003 ten-spot installation after five hours, wrong Wi-Fi, failed preview and
  retry; no failed candidate is committed and all ten spots survive success.
- Setup-screen buffer bounds and native black/white encoding for all three panels.

Both the universal E1001/E1002 firmware and the separate E1003 firmware compile.
E1003 was built in an isolated build directory to avoid changing the E1002 test
bundle. E1001/E1003 physical hardware was not available.

## Physical E1002 results

The final test bundle is `dev-local-20260920-184056`. A full erase/clean install
was followed by E1002 profile selection and reboot, a network scan, 130 seconds
with no commands, a rejected password, and an intentional apply without internet.
That apply returned `render_failed`, transport error 28674, zero response bytes,
`wifiConfigured:false` and `render:pending`; it did not update the panel.

Submitting the correct network immediately afterward fetched the Edam forecast
with HTTP 200 (10,555 bytes), validated it, refreshed the physical panel and
committed the setup. `get_state` confirmed `apply:complete`, `wifi:connected`,
`wifiConfigured:true`, `render:valid` and `applyError:0`.

Reapplying on that same final firmware also succeeded. The reserved worker was
reused (15,080 bytes minimum free stack after the successful preview), with the
largest free internal block stable at 45,056 bytes across the two successful
applies. A subsequent reboot reconnected to saved Wi-Fi, retained the exact Edam
configuration digest and reported a valid screen. NTP timed out during this
reboot; the retained RTC clock and cached forecast still worked correctly.

Earlier preserving-update and repeated-apply tests also succeeded after the
memory correction. Five hours was tested with simulated time, not a five-hour
wall-clock hardware soak. The actual hardware pause crossed the former
two-minute expiry.

The real Dia click-through for updating configuration passed against the physical
E1002: Edam Noord - Galgenveld was selected in the configurator, the USB device
was selected, setup was applied, and forecast verification reached
“Ready for the wind”.

A second Dia run started with fully erased flash. The browser identified the
blank E1002, flashed the local bundle, reconnected, selected its hardware profile
and presented Wi-Fi setup. The form remained open for more than two minutes
(first observed at 19:02:15 UTC; credentials submitted after 19:04:29 UTC).
Submitting the provided network then completed Wi-Fi, configuration and first
forecast verification, reaching “Ready for the wind” again. The Done button
closed the installer and released the serial port. No new production Sentry
test event was sent.

## Sentry interpretation

- [WINDPEEK-5](https://windscout.sentry.io/issues/WINDPEEK-5) contains a roughly
  ten-minute pause at Wi-Fi followed by a `begin` timeout. This supports testing
  the idle-session path; it does not prove all later failures share that cause.
- [WINDPEEK-6](https://windscout.sentry.io/issues/WINDPEEK-6) contains repeated
  successful handshakes followed by failed status requests.
- [WINDPEEK-4](https://windscout.sentry.io/issues/WINDPEEK-4) reports `commit_failed`
  without enough evidence to determine its underlying failure.

New error reports include apply/transport/parser error codes, HTTP status,
response size, reset reason, device clock and available internal memory. No raw
serial logs, Wi-Fi credentials, network names or forecast response bodies are
uploaded. Historical events are not marked resolved by this local test.

## Review boundary

Targeted manual review covers these installer changes. File-wide simplification
was skipped where it would overlap pre-existing edits. Changes remain local;
this test does not publish firmware or update the public installer.
