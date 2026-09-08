---
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: session
execution: code
title: Edam production dashboard reliability
date: 2026-08-24
---

# Edam production dashboard reliability

## Goal Capsule

Deliver one trustworthy Edam dashboard on the E1002: five days of real KNMI Seamless forecast data, an honest indication when retrieval fails or cached data ages, and predictable refresh/day-rollover behavior without unnecessary E-ink refreshes.

In scope: product priorities 1, 2 and 5 from the session. Multi-spot navigation, device setup, and production OTA hardening remain out of scope.

## Product Contract

### Requirements

- **R1 — Real Edam data.** Every populated value comes from the configured live Open-Meteo `knmi_seamless` response for Edam (`52.5126, 5.0486`), never from sample data.
- **R2 — Stable five-day view.** The dashboard shows today plus the next four local calendar days, with five daytime samples per day, sustained wind, gusts and destination-direction arrows.
- **R3 — Honest status.** Fresh, aged, stale, offline-after-failed-refresh, and unavailable-without-usable-data are distinguishable. A failed refresh may keep valid cached values visible but must not silently look live.
- **R4 — Traceable data age.** The header shows the timestamp of the forecast response currently being displayed. Stale/aged labels use elapsed age from that timestamp.
- **R5 — Deliberate refresh schedule.** Automatic data attempts occur at local `00:05`, `07:00`, `11:00`, `15:00`, and `19:00`. Manual refresh remains available. No scheduled network work is added after 19:00 before the midnight rollover attempt.
- **R6 — Correct day rollover.** The first column is always `TODAY`. If the cached forecast no longer starts on the current local date, one recovery fetch is attempted without entering a retry loop.
- **R7 — E-ink restraint.** A successful run only refreshes the panel when the final bitmap differs from the last confirmed bitmap.
- **R8 — Battery truth.** The real battery reading is rendered; an unknown reading is not fabricated and low charge remains visibly recognizable.

### Acceptance Examples

- **AE1:** A successful Edam fetch at 07:00 publishes and renders the returned values, model, coordinates and retrieval timestamp.
- **AE2:** A scheduled fetch fails while a three-hour-old cache exists; cached values remain visible and the header reports `OFFLINE`.
- **AE3:** A cache is 8 hours old without a new failed attempt; the header reports `AGED 8H`.
- **AE4:** A cache is at least 24 hours old; the header reports `STALE <age>H`, including after another failed attempt.
- **AE5:** No valid cache and a failed fetch render `FORECAST UNAVAILABLE`, with no invented bars or arrows.
- **AE6:** At 00:05 after the date changes, the first column becomes `TODAY` for the new date and the five-day window is replenished.
- **AE7:** Two runs producing the same bitmap cause only the first physical E-ink refresh.

## Key Technical Decisions

- Keep data freshness separate from refresh connectivity. A failed network attempt is not the same property as an old forecast; the renderer receives both signals and applies a clear priority.
- Preserve the existing cache-first architecture. Failed requests never erase the last valid forecast.
- Preserve the existing five fixed local-time boundaries and timezone-aware `mktime` handling.
- Extend the existing monochrome renderer and tests rather than introducing a second dashboard path.

## Implementation Units

### U1 — Carry refresh failure into rendering

Files:
- `firmware/main/wind_app.h`
- `firmware/main/wind_app.c`
- `firmware/host_tests/test_wind_app.cpp`

Extend the render contract with whether the current run attempted and failed to retrieve data. Confirm cached data is still rendered and the condition reaches the renderer. Covers R3, R6 and AE2–AE5.

### U2 — Render honest status priority

Files:
- `firmware/main/wind_renderer.h`
- `firmware/main/wind_renderer.c`
- `firmware/host_tests/test_wind_renderer.cpp`

Add the offline signal to the dashboard model. Render unavailable first, stale second, offline third, aged fourth, and normal model status last. Keep status right-aligned and clipping-free. Covers R3, R4, R8 and AE2–AE5.

### U3 — Lock live-data and schedule contracts

Files:
- `firmware/host_tests/test_wind_provider.cpp`
- `firmware/host_tests/test_wind_schedule.cpp`
- `firmware/host_tests/test_wind_app.cpp`

Strengthen contract tests for Edam identity, five-day sample selection, the five agreed refresh boundaries, date rollover recovery, and unchanged-frame suppression. Covers R1, R2, R5–R7 and AE1, AE6, AE7.

### U4 — Verify on the real Windpeek

Files:
- `firmware/scripts/ota-upload.sh`

Run host tests and a complete E1002 build, install the new version over Wi-Fi, then verify the device reports the new firmware and serves its HTTP status after reboot. Compare a live Edam response with the rendered/cache path; do not claim forecast correctness from compilation alone.

## Verification Contract

- All registered host tests pass.
- The E1002 firmware build succeeds with the intended version in its app descriptor and fits both OTA partitions.
- A live provider request for Edam returns five valid local dates and the expected daytime sample hours.
- Wi-Fi OTA validates, reboots and reports the new version at `windpeek.local`.
- Renderer tests cover fresh, offline-with-cache, aged, stale and unavailable states without clipped primitives.

## Definition of Done

- No sample forecast can reach the production renderer.
- A failed scheduled refresh is visible without deleting usable cached data.
- Day rollover and all five boundaries remain timezone/DST-safe.
- Identical frames remain suppressed.
- The tested firmware is running on the user's Windpeek.

## Deferred

- Multiple spots and button navigation.
- Consumer setup and spot selection.
- Re-enabling production power/OTA restrictions; the current always-on development switch remains explicitly temporary.
