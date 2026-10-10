# Display recovery, device errors and OTA — 10 October 2026

## Confirmed defects and changes

- A failed scheduled display write did not arm the existing five-minute retry.
  A regression test reproduced the next wake being the ordinary forecast boundary.
  Display and download failures now share one persisted retry, without rearming it
  from the retry itself.
- The previous panel confirmation remained valid while a new waveform ran. A
  second regression test observed that stale confirmation inside the display
  callback. It is now cleared before the hardware write and restored only after
  success, so a reset cannot leave an interrupted write marked complete.
- Firmware previously had no application OTA client. The new client uses the
  existing release feed, checks compatibility and hashes, writes the inactive
  partition and confirms the next boot after a successful dashboard refresh.
  Forecast/display failure does not prevent checking for a corrective update.
- Forecast, render, normal/prepared display, OTA and abnormal-reset errors are
  retained in bounded NVS records and delivered directly to Sentry. Reports omit
  credentials, location, raw logs and stable device identifiers. Delivery is
  limited to one attempt per category per hour and uses existing connected work.

The original report concerned a probably-E1002 panel that remained blue for
hours and recovered after manual refresh. Its initial trigger has not been
reproduced. These changes address confirmed recovery gaps; they do not detect
incorrect pixels when the controller reports success.

## Automated verification

- Before the retry follow-up, 383 firmware host tests passed, including interrupted drawing, persisted
  retries, offline reporting, throttling, manifest validation and the actual OTA
  implementation compiled with controlled HTTP/flash/NVS dependencies.
- 55 focused tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
  The SHA-256 platform implementation is mocked in OTA host tests; hardware tests
  exercise the real implementation.
- 886 web tests passed; production web build passed with its existing chunk-size
  warning. All 13 Python build/installer tests passed.
- E1003 and universal E1001/E1002 firmware builds passed. Installer stack checks
  remained below their 12,288-byte budget plus 4,096-byte reserve.
- Production feed URLs returned HTTP 200 without redirects. Existing devices
  need a USB update containing the rollback-enabled bootloader before gaining OTA.

## Physical E1003 verification

The connected device identified itself as E1003, initially running
`dev-ac4bf3b1-powerlog`. Its CH340 bridge was accessible through libusb, although
macOS exposed no ordinary serial port. No browser was needed.

Before flashing, bootloader/settings and both application slots were backed up
outside the repository, with complete hashes checked against the device. The
active application's SHA-256 was
`8d81f5f92330b04963faf51a52ad24e612325ebdfcbe31af39b74981fdcf5d78`.
The partition table matched the new build. The USB bootstrap changed only the
bootloader and active application; partition table, NVS and boot selection were
verified unchanged.

A temporary bootstrap used a local HTTP fixture feed and one explicitly injected
runtime error. These test modifications are absent from the source changes and
the final application. The good application uses the production HTTPS feed.
The NVS backup was restored between independent OTA scenarios to reset the daily
check timer while keeping the same installed settings.

- The device's synthetic runtime event arrived in
  [WINDPEEK-M](https://windscout.sentry.io/issues/WINDPEEK-M).
- A deliberately corrupted image had correct manifest and transport hashes, but
  failed ESP image verification with `ESP_ERR_OTA_VALIDATE_FAILED`. The boot
  selection remained byte-for-byte unchanged. This real device error arrived in
  [WINDPEEK-N](https://windscout.sentry.io/issues/WINDPEEK-N).
- The valid `dev-screen-ota-d` application downloaded over Wi-Fi into `ota_1`,
  passed manifest/application/image checks and restarted successfully. The device
  then reported the new version, connected Wi-Fi, a valid dashboard, unchanged
  configuration digest and zero refresh/fetch errors.
- Reading the physical OTA selector confirmed sequence 2 with state 2
  (`ESP_OTA_IMG_VALID`); the prior slot retained sequence 1, also valid. The device
  was restarted after this read and remains on the new application. The previous
  battery-diagnostic firmware is retained in the verified private backup.

The E1002 color panel, long-duration battery operation, physical interruption
during its waveform and physical rollback after a crashing candidate were not
tested. Rollback API behavior and rejected downloads are covered by host tests.

## Review

The fix-owned changes were reviewed sequentially for reuse, correctness,
persistence, memory/resource bounds, release compatibility and failure paths.
No subagents were used. The review tightened the manifest's array and sequence
validation, removed duplicate reporting on scheduled writes, and included direct
prepared-screen writes. Unrelated battery-investigation edits in the original
working directory were left untouched.


## Follow-up: faster display recovery

Requested after the initial implementation: retry display failures sooner, then
once more at five minutes. Display failures now schedule recovery at +30 seconds
and +300 seconds from the original cycle timestamp. The last attempt remains
anchored to that timestamp even when the first retry runs late. If both deadlines
were missed, only one recovery runs. A regular forecast boundary supersedes the
old retry budget. Download-only failures still get one five-minute retry.

The remaining attempt is persisted before the fast retry begins and retained
through the physical panel write. A successful fetch and display cancel it;
a second display failure consumes the budget. Schedule schema 4 rejects and
reinitializes older scheduling records on upgrade, triggering an ordinary fresh
cycle while preserving installation settings and forecast caches.

Validation: the three changed regression tests failed against the previous
implementation, then passed. The full host suite passes all 385 tests, including
new late-wake/final-success and missed-deadline regressions. E1003 firmware build
`dev-screen-retry-e` (OTA sequence 5) passes. Reviewed the retry budget, persisted
state, successful recovery, and normal-boundary supersession in the scoped diff.
The timing policy is verified with host tests; the original visual fault has not
been reproduced on hardware.

The 37 WindApp tests also pass under AddressSanitizer and UndefinedBehaviorSanitizer.
Installed the follow-up by USB into the verified active OTA slot. Application
MD5 matched after writing; partition table, NVS/settings and boot selector hashes
were unchanged. Flash evidence: `/tmp/windpeek-usb-flash-retry.log`.

Post-flash installer protocol confirmed `dev-screen-retry-e`, connected Wi-Fi,
`render=valid`, refresh/fetch errors zero, HTTP 200, and the same configuration
digest as before. Evidence: `/tmp/windpeek-retry-device-ready.log`.


## E1002 refresh-duration investigation before publication

A firsthand E1002 bench report published September 29, 2026 describes 30–44 second
refreshes and a 40-second timeout cutting the wait short:
https://www.hackster.io/melastmohican/driving-the-reterminal-e1002-7-3-color-e-paper-with-arduino-21070f
Our ED2208 driver used that same 40-second limit. The refresh phase now allows
60 seconds; reset, data and power transitions retain 40 seconds. Timeout logs
and Sentry diagnostics report the applicable limit. This mitigates a specific,
externally observed failure mode; it does not prove the original user's cause.
The connected E1003 cannot validate this different E1002 panel controller.

Other unresolved hypotheses: delayed/missing BUSY assertion can be accepted as
success, SPI operates at 20 MHz versus Seeed_GFX's 10 MHz reference configuration,
and a panel-supply dip can interrupt the waveform without an ESP reset. Normal
work and light-sleep locks already protect refreshes from the ordinary sleep
timer. None of these hypotheses is established by the supplied photos.

Durable USB evidence summary: application MD5 `a7b7ab1fda0d646aa1938de6739cb407`;
firmware `dev-screen-retry-e`; render `valid`; Wi-Fi `connected`; refresh/fetch
errors `0`; HTTP `200`; uptime `105726` ms. Configuration digest matched the
pre-flash value. The temporary logs are supplemental, not required for this record.

A simulated 44-second refresh timed out with the old 40-second budget and
completed with the new 60-second budget. A simulated 70-second busy signal
still timed out at 60 seconds.

### Release review follow-up

- 391 host tests pass after adding cached-display and forced-redraw recovery,
  invalid telemetry-record recovery, multi-chunk OTA, sequence persistence failure,
  and declining-battery coverage. Firmware tooling: 14 tests; manifest validation:
  15 web tests.
- Telemetry releases its mutex during network delivery and caches whether each
  stage has pending data, so concurrent display-error capture does not wait for
  an HTTPS request and idle flushes do not repeatedly scan NVS.
- Firmware artifacts carry the compiled OTA sequence into manifest generation;
  production cache entries are scoped to the release run.
- The browser USB installer still erases Wi-Fi/settings; the earlier physical
  preservation check used the explicit preserving write set.

- Full web suite: 887 tests pass. Focused app/reporting/OTA sanitizer suite:
  63 tests pass. E1002 build and installer stack budgets pass.
