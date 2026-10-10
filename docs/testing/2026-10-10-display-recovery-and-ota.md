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

- 383 firmware host tests passed, including interrupted drawing, persisted
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
