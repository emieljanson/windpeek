# Installer resilience review — 2026-09-20

Scope: installer changes ported onto `b34fd26` (current main), preserving its
UART reception, reserved worker, stack guards, crash evidence and panel-lock
fixes. Unrelated work in the older local checkout is excluded.

## Findings resolved

1. **Mixed protocol and device ownership.** The installer service had 1,102 lines,
   mixing JSON validation with UART, Wi-Fi rollback, task lifetime and rendering.
   The portable protocol service is now separate from the ESP-IDF device adapter.
   The existing dependency boundary remains the only boundary; no new dispatcher
   or generic task framework was added.
2. **Boolean-mode growth in the forecast cycle.** Adding setup validation to the
   existing three booleans obscured which combinations were valid. The portable
   cycle now names four operations: refresh, prefetch, cached display and setup
   verification. Device runtime and portable cycle compile separately and reuse
   the same freshness/coverage helpers.
3. **Duplicate diagnostics contracts.** The earlier patch introduced a second
   nested device snapshot and repeated allowlists through the serial adapter and
   two privacy filters. The release extends the existing flat health snapshot
   and its canonical sanitizer instead. Provider diagnostics use a provider-owned
   type, avoiding a concrete Open-Meteo type in the application interface.
4. **Stale failure evidence.** A preview failing before a fetch could inherit the
   previous fetch's HTTP status. The runtime now returns the complete cycle
   outcome; diagnostics are captured only for an actual provider request, while
   the runtime lock still protects the snapshot.
5. **Unnecessary internal-memory pressure.** Approximately 60 KiB of task-owned
   installer buffers occupied internal RAM. Those buffers move to PSRAM; the
   existing preallocated apply stack stays in internal RAM for flash operations.
   Failed Wi-Fi tests reuse the same rollback path as cancellation.

## Behavioral guarantees

- First boot waits for persisted setup rather than fetching default-location data.
- USB sessions tolerate human delays; idle battery sessions still clean up.
- Setup success requires fetched, validated data and confirmed panel output.
- A failed fetch leaves the candidate uncommitted and does not replace the panel.
- Wrong-board build configuration fails before a potentially wrong flash bundle.
- E1001, E1002 and E1003 share these changes.

## Validation

- Web unit suite: 682 tests passed, including five-hour Wi-Fi delays for each model
  and adapter-to-Sentry privacy filtering of forecast errors.
- Firmware host suite: 339 tests passed, covering failed/late setup, retry, panel confirmation, all
  panel palettes, open-network readiness, ten-spot E1003 setup and existing
  navigation/cache behavior.
- E1001/E1002 and E1003 ESP-IDF builds passed. Installer stack estimates remain
  below 12,288 bytes, reserving another 4,096 bytes in the 16 KiB UART task.
- Renderer reproducibility, web build, build-script and manifest tests passed.
- Physical E1002 preserving update and reapply: saved Wi-Fi reconnects, apply
  completes, the forecast returns HTTP 200 and `render:valid`. The first reviewed
  build retained 15,184 bytes minimum apply stack after rendering.
- Before this review, the full clean-install flow was exercised in Dia with a
  greater-than-two-minute Wi-Fi pause. Five-hour delays use simulated time.
- E1001/E1003 physical devices were unavailable. Historical Sentry incidents are
  not all attributed to one cause and are not marked resolved by these checks.

## PR review follow-up

Thirteen additional comments were addressed: wait for installer completion on
first boot, retry a failed setup hint without overwriting an active preview,
preserve legacy APSTA retry behavior, clear unstarted apply buffers, retain the
idle-timeout checkpoint, bound signed error codes, and tighten tests and API
comments. The storage-failure tests now explicitly distinguish a failed schedule
write before fetching from a failed cache write after fetching.

The proposed schedule-boundary fallback was not added: `wind_schedule_is_due`
always supplies the current boundary for an initialized schedule, including when
it returns false. `FailedOutOfWindowRecoveryRetriesOnceAfterFiveMinutes` already
covers the reported scenario and passes.
