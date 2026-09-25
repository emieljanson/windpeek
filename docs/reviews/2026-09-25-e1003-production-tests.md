# E1003 production test report — 25 September 2026

**Current release status: publication authorized; final corrective commit awaiting
release gates.** The complete PR workflow passed at `bf931102`, including all
four browser shards, both firmware targets and the full sanitizer suite.
Follow-up review fixes add three regression tests, bringing the host suite to
337 tests.
The sections below are a chronological test log; earlier pending checks and
“nothing published” statements describe their stage, not the current status.
Battery-only sleep/wake was confirmed by the user. A physical retest of the
navigation fix and worst-case concurrent heap headroom remain unverified;
automated success does not establish zero defects.

## Confirmed defects fixed

- E1003 runtime incorrectly requested three days while the product supports one
  day, five days, or overview. The validator was correct to reject three. The
  first attempted fix incorrectly expanded the validator; user feedback caught
  this product-contract mistake. Runtime, touch targets and prepared day views
  now use five days; the validator again rejects three. Invalid render input
  still returns an error without replacing the panel with fabricated unavailable
  data or an unknown battery.
- Prepared spot screens ignored changing battery levels and forecast age.
  Cache identity now includes battery, age, freshness and the local date,
  including independently aging swell data. Source matching is exact; a
  previously rendered forecast is no longer accepted just because it is less
  than six hours behind the current data. Old cache formats/render signatures
  are invalidated.
- After an offline midnight, detail could label yesterday's samples as today
  while overview used the current date. The dashboard calendar and day-detail
  selection now align to the spot's current local date.
- Renamed spots could display the old name from forecast cache. The installed
  spot configuration now supplies the title.
- Background setup could abandon missing forecasts after its ordinary retry
  was spent, fail to resume after restarting, or treat failed swell/tide writes
  as complete. Missing data remains retryable; navigation considers missing
  marine data and expired wind data.

- A background download could publish a newer forecast while an older frame was
  being rendered. Saving the frame then re-read the source and incorrectly marked
  the old pixels as current. Spot images now retain the actual render input;
  overview images retain the source identity captured before rendering. A
  truncated compressed image is replaced after fallback rendering.

## Automated evidence

| Check | Result |
| --- | --- |
| Firmware host suite (`make -C firmware test`) | 334 tests passed |
| Embedded E1003 runtime flows, included above | 21 tests, including 72 module combinations and 100 offline navigation rounds |
| Web unit suite (`cd web && npm test`) | 819 tests across 73 files passed |
| Python build/installer tooling tests | 10 tests passed |
| Production web build | Passed; existing large-chunk warnings remain |
| E1003 firmware build | Passed |
| Universal E1001/E1002 firmware build, separate build directory | Passed |
| AddressSanitizer + UndefinedBehaviorSanitizer | Full 334-test host suite passed with ASan + UBSan, no diagnostics |
| Generated WebAssembly renderer consistency | Passed |
| Dependency audit after updates | 0 known vulnerabilities reported |

The embedded runtime tests compile `wind_app.c` through its **ESP_PLATFORM**
path, rather than the host implementations that return `ESP_ERR_NOT_SUPPORTED`.
They use the real renderers, forecast/swell/tide caches, gzip screen caches,
spot configuration and scheduling code. HTTP, clock, panel hardware and locks
are controlled test substitutes. Storage uses an isolated temporary directory.

Covered flows:

1. Install ten spots, fill their forecasts, open each spot and all five hourly
   views, return to overview, all with Wi-Fi subsequently unavailable.
2. Fail one spot twice, let other spots load, restart, restore connectivity and
   recover the missing forecast.
3. Preserve selected spot and overview page across a simulated sleep/wake.
4. Fail a display write; retain the previous selection/panel and retry.
5. Cross a day boundary offline, retain usable cached data, reconnect and refresh.
6. Change battery level or forecast/swell age without changing source data;
   reject stale prepared screens.
7. Render all wind/swell sizes and combinations of weather, temperature and tide.
8. Corrupt a compressed screen and recover from forecast data while offline.
9. Reinstall with a renamed spot and changed display preferences.
10. Fail a candidate installation and keep the previous configuration usable.
11. Publish a newer forecast during detail rendering or overview display;
    reject the old image on the next visit.
12. Navigate while a background fetch has released the runtime lock; retain
    the new selection after the fetch completes.
13. Truncate a compressed screen and verify that fallback repairs the file.

## Browser testing and remaining checks

The first Dia run passed 35/36 tests; a panel-height assertion sampled an active
animation. The next passed 33/36: one popup-position assertion sampled before
positioning settled and two tests lost their browser contexts. The geometry
assertions now wait for the same strict final values rather than accepting a
wider tolerance. **They have not been revalidated in a final full run.**

The user requested reuse of the existing Dia window. Separate browser launches
were stopped. A new tab was opened there, but the running Dia instance has no
debugging endpoint and rejects AppleScript JavaScript without a launch flag.
It was not restarted or reconfigured. The test runner must not launch more local
browsers as a workaround. The existing CI browser suite can verify the final
changes separately; this report does not claim it has run.

The E1003 has no macOS serial device, but its CH340 USB bridge was found
(VID `1a86`, PID `7522`). A direct libusb protocol query succeeded: board
`seeedstudio_reterminal_e1003`, configured Wi-Fi, disconnected Wi-Fi at that
instant, valid render status and idle installer. The configuration digest was
`f37b352216b66932`; free internal heap was 62,651 bytes, minimum 23,607 bytes,
and task stack free was 12,976 bytes. These are one-time diagnostics, not a
completed physical stress test. Reopening USB initially failed; after reconnecting, ROM access and partition
inspection succeeded. The active slot is `ota_0` at `0x20000`, size `0x380000`,
with a valid OTA selector. The default flasher read stream lost bytes across
multiple baud rates; a temporary direct-USB adapter now limits the flasher to
one outstanding 1 KiB read packet and verifies each read digest. The complete original app backup was verified against the device MD5 before
writing; SHA-256 is `f1449ae8c2bcb9b74bb34dd351dcaf1972f1b3291c596ae89e063f744928f699`.
Only `ota_0` was written, and the resulting firmware hash was verified. The OTA
selector was unchanged. Backup files and the test binary are retained at
the maintainer-retained E1003 backup bundle dated 2026-09-25.
After restart, the device reported firmware `dev-state-tests-20260925`, the
unchanged configuration digest `f37b352216b66932`, connected Wi-Fi and a valid
render at approximately 33 seconds uptime. Refresh, fetch, transport and parse
error counters were zero. A second hardware reset preserved the firmware and
configuration again. At 14 seconds it reported a valid cached render while Wi-Fi
was disconnected; by 67 seconds Wi-Fi was connected and a real forecast refresh
returned HTTP 200 with zero refresh/fetch/transport/parse errors. Physical
screen/touch confirmation remains pending.

The second boot reported `minimumHeap=23`, current free internal heap 31,175
bytes and largest internal block 7,680 bytes at the final sample. ESP-IDF
`heap_caps_get_minimum_free_size` sums independent per-region low watermarks;
23 bytes does **not** prove that total free memory simultaneously reached 23
bytes. No allocation failure or crash was reported, but this warrants further
concurrent navigation/download memory profiling before production approval.
The original direct-USB reopening timeout was traced to the test script: it
restarted request IDs at one with `get_state`, whereas the protocol requires
`hello` at request one to open a new session. With the proper handshake, five
consecutive close/reopen cycles passed without resetting the device or changing
the configuration. This is distinct from physical cable removal and battery wake.

Outstanding at this stage (later verification is recorded below):

- Verify actual panel content, battery display, touch coordinates and every
  spot/day on the flashed test build.
- Exercise physical button/touch wake, timer wake, USB removal, battery operation
  and Wi-Fi loss/recovery, including interrupted initial setup loading.
- Investigate internal heap headroom under concurrent navigation/download load.
- Stress navigation while background downloads and prepared-frame generation
  run. The host harness covers deterministic callback interleavings but does not
  prove real FreeRTOS scheduling,
  task stack margins, SD-card power-loss behavior or real HTTP/TLS operation.
- Run the complete browser suite on the final dependency versions and changes,
  including the real map flow. Component tests mock the WebGL map boundary.

## Dependency updates

The audit identified [MapLibre's attribution sanitizer bypass](https://github.com/advisories/GHSA-jrc7-96c5-q579)
and [Vitest's mock-server path traversal](https://github.com/advisories/GHSA-82fw-gwwq-j7x9).
MapLibre is pinned to patched 6.4.1; Vitest to patched 4.1.11. Unit tests and the
production build pass with those versions. Zero audit findings is not a complete
security audit.

## Reproduce the memory checks

```sh
cmake -S firmware/host_tests -B firmware/host_tests/build-sanitizers \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build firmware/host_tests/build-sanitizers --target e1003_runtime_test -j4
firmware/host_tests/build-sanitizers/e1003_runtime_test
```

## Five-day correction after physical feedback

The initial `dev-state-tests-20260925` test build exposed an incorrect three-day
layout. The corrected build is `dev-five-day-fix-20260925`. The runtime regression
now visits all five days for every installed spot; touch tests check both edges
of all five columns. Render signature is bumped to invalidate prepared three-day
images. Earlier automated passes did not establish the correct product contract.

The corrected five-day build passed all 324 host tests, all 16 runtime tests
under ASan/UBSan, both firmware builds and the WebAssembly renderer rebuild.
It was flashed app-only and verified by hash. USB hello confirms
`dev-five-day-fix-20260925`; configuration digest remains `f37b352216b66932`.
The device reported a valid render at 25 seconds with no refresh error. The
three-day runtime branch, three-column hit targets and three-day prepared-view
limits have been removed. A negative test explicitly rejects three days.

## Additional recovery testing

New host coverage verifies prepared fifth-day screens across reboot, focus
rollback after failed panel writes, selection retention after failed overview
display, and 100 offline navigation rounds with exact screen hashes and no
additional downloads. ASan/UBSan reported no diagnostics.

Two additional USB defects were reproduced before fixing them:

- A truncated request retained its advertised payload length indefinitely and
  blocked a subsequent session. Unfinished bytes now expire after three seconds
  of idle UART input, preserving replay protection. Queued chunks are not
  discarded while they are still arriving.
- An oversized header returned early from the parser and discarded valid frames
  later in the same UART read. The parser now continues processing after the bad
  header. A valid recovered frame also prevents the earlier framing error from
  cancelling the session it has just opened.

The second defect reproduced on the physical device as a `hello` timeout as well
as in the host regression. Final hardware fault-injection results follow below.

### Final physical recovery results

Verified app-only flash and USB hello for `dev-recovery-tests2-20260925`, with
configuration digest `f37b352216b66932` unchanged. Passed on the physical E1003:
noise before a frame, invalid CRC, oversized header followed immediately by a
valid hello, reopening after truncated 13-byte and 100-byte requests, and a
valid request split into 3-byte chunks. No reset was needed between cases.

Five Wi-Fi scans returned valid CRC-protected responses (six networks, 350-byte
responses); this does not test the maximum possible scan-response size. Across
46 status samples spanning 118.1 seconds,
current internal free heap ranged from 42,359 to
57,807 bytes and ended at 57,807 bytes.
The smallest sampled largest allocatable block was 24,576 bytes.
The device completed a real HTTP 200 refresh and reported no refresh, fetch,
transport, parse or provider allocation errors. Uptime increased continuously.
No sustained heap decline was observed in this short test. The aggregate
historical low watermark again reached 23 bytes; this does not establish the
instantaneous minimum or resolve worst-case concurrency headroom.

Physical touch actions were requested during monitoring but no confirmation
was received; these measurements must not be described as a verified physical
navigation stress test. At this stage, battery-only sleep/wake, cable removal, interrupted
initial setup, peak concurrent heap usage, and the final browser suite had not
yet been verified. Logs, the summary and temporary direct-USB test scripts are retained in
the maintainer-retained E1003 backup bundle dated 2026-09-25.


## Follow-up: physical wake, touch-state race and cache memory

The user completed battery-only sleep (USB disconnected for at least 2½ minutes)
and wake, and reported that it worked. USB state after reconnect reported reset
reason 8 (deep-sleep wake). The user also reported that tapping a spot sometimes
refreshed overview instead of opening the spot. Button wake still needs a separate
physical check: the white wake button's boot path explicitly opens overview;
the e-ink image itself remains visible while asleep.

A matching touch-state race was reproduced in a new embedded runtime regression.
After navigation opens overview, a background fetch can acquire the runtime lock
before the input task reads the new view. The nonblocking getter then fails and
input uses its previous detail-view state, mapping the tap to OPEN instead of
SELECT. The getter now reads an atomic snapshot containing the displayed view
and page together, published after successful navigation/display. It remains
available while background work holds the runtime lock. The regression failed
before the fix and passes after it; it also checks the reverse transition and
retains the detail snapshot when an overview display fails. This establishes a
code defect matching the symptom, not a captured trace of the user's exact tap.

The full host suite now passes 332 tests. All 21 embedded runtime and 7 USB tests
also pass with AddressSanitizer/UndefinedBehaviorSanitizer. E1003 and universal
E1001/E1002 builds pass.

Compressed screen caches now use a 1 KiB zlib I/O buffer instead of the default
8 KiB. A native benchmark using the vendored zlib, a full packed E1003 frame,
and exact decompressed-byte comparison measured 21,504 fewer allocated bytes
for both reading and writing. The gzip format is unchanged. These are native
allocation measurements, not proof of the physical device's peak heap margin.

In the user's existing Dia window, a temporary isolated-origin harness passed
real WebGL rendering, independent settings for two spots, and installer stages
from confirmation through writing, Wi-Fi and completion. The device session,
forecast HTTP responses and geocoding are simulated; the browser, application,
WebAssembly renderer and transitions are real. The tab must remain foreground
for animation completion. This is additional browser smoke coverage, not a
replacement for the entire 36-test Playwright suite.


### Current device build and verification

Installed `dev-navigation-tests-20260925` using an app-only flash. Both the
previous and written app were MD5-verified; partition layout, OTA selector and
configuration digest `f37b352216b66932` remained unchanged. This build includes
the touch-state race fix and smaller gzip I/O buffers.

Five Wi-Fi scans and 46 status samples over 118.2 seconds passed without a
reset, changed configuration or reported refresh/fetch/transport/parse/allocation
error. Sampled free internal heap ranged from 22,571 to 140,799 bytes, ending at
27,531 bytes; smallest sampled largest block was 15,360 bytes. Historical
aggregate low watermark ended at 15,939 bytes. These samples include startup
and cache preparation and are not a like-for-like comparison with the earlier
already-running build. They do not establish peak concurrent memory safety.
The subsequent physical USB fault suite also passed noise, CRC corruption,
oversized headers, two truncated requests/reconnects, and fragmented requests,
without a reset. A post-fix physical touch retest was requested and is pending.

### MapLibre worker regression found and fixed

MapLibre 6.4.1 derives its default module-worker URL relative to its main module.
Vite's dependency prebundling and production chunk names break that location.
The first empty-style smoke test passed despite a missing worker, so it was
strengthened with a GeoJSON point and circle layer. That test failed before the
fix: Add spot never enabled, and Vite reported a missing worker module.

`geoapifyMap.js` now imports the worker through Vite's `?worker&url` bundling
and explicitly calls MapLibre's `setWorkerUrl` before creating a map. The
production output now contains the worker with its dependencies bundled.
The existing browser fixture also contains GeoJSON so it cannot mask this
failure with an empty style.

After the fix, the existing Dia tab passed the foreground browser smoke test
with the actual map worker: WebGL forecast, independent spot settings,
simulated installer, map initialization and personal-spot forecast. A separate
run against the built production files passed forecast, independent settings
and map/personal-spot creation with no captured browser errors. That test build
uses a dummy Geoapify key with mocked HTTP responses and locally supplied
GeoJSON; it does not verify real tile service availability. All 819 web unit
tests passed again, as did the production build. Nothing was published.


## Further release testing: USB replay protection and full sanitizer suite

The USB parser reset its remembered request ID when it rejected an oversized
header. An already accepted command could consequently be accepted twice after
serial corruption. A new host regression failed before the fix (two callbacks
instead of one). Framing recovery now clears only accumulated bytes; it retains
request replay protection. An explicit request-1 hello still starts a new
browser session. A separate regression passes a full 16,384-byte binary payload
through deterministic varying chunk boundaries, followed by another frame, and
checks exact payloads and request IDs.

All **334 firmware host tests** pass, both normally and as a complete suite
compiled with **AddressSanitizer and UndefinedBehaviorSanitizer**. This expands
sanitizer coverage beyond the 21 embedded runtime and now 9 protocol tests to
all host test executables, including installer, configuration and cache tests.
Hardware services are still substituted where required; this is not execution
of every ESP-IDF path or every possible task interleaving. Both E1003 and
universal E1001/E1002 builds pass.

Installed `dev-release-tests-20260925` on the E1003 with the same app-only,
MD5/partition/OTA checks. Configuration digest remains `f37b352216b66932`.
On-device boundary tests passed:

- After an oversized header, replaying an earlier read-only get_state request
  produces no duplicate response; the next fresh request succeeds.
- A maximum-size 16,384-byte JSON request sent in 487-byte chunks succeeds,
  followed by an ordinary request.
- A corrupt-CRC request immediately followed by a valid request recovers without
  reconnecting or resetting.
- The prior noise, bad CRC, oversized header, two truncated-message reconnects
  and three-byte fragmentation suite also passes on the new build.

These physical tests use read-only requests to verify replay behaviour;
they do not deliberately repeat configuration writes on the user's device.
At the end of the boundary test, current internal heap was 52,415 bytes,
largest free block 31,744 bytes, and all reported refresh/fetch/transport/parse/
allocation errors were zero. Continuous uptime and the unchanged configuration
were asserted.

Additional real-Dia browser scenarios passed using the existing tab:

- Production files: per-spot settings survive reload, removal restores the
  remaining spot, ten-spot limit rejects an extra spot and supports removal/
  re-addition, and E1001/E1002/E1003 previews switch without reloading.
- Production files: first forecast failure is labelled as demo, unsupported
  tide stays disabled, and mobile controls fit at 390×844 and 390×700.
- Development fixture: diagnostics link includes a valid reference and leaves
  recovery usable; Wi-Fi scan failure recovers through rescan and completion.
- Production files: model popup selection updates the live forecast, stays
  inside the viewport and restores focus after Escape.
- Development fixture: expanded settings collapse during installation and
  restore focus on close; the installer fits a 640-pixel-wide desktop and
  prevents closing while the simulated firmware write is unsafe to interrupt.

Forecast responses and device sessions are fixtures. Synthetic DOM events do
not replace trusted-input/browser-permission tests or the complete Playwright
suite. Several initial temporary-harness failures were corrected selectors,
timing, and an intentionally invalid-length diagnostic reference; they were
not product defects. No additional browser product defect was established in
these scenarios.


The final additional production-browser scenario also passed: wind threshold
retains its value when hidden/shown and after reload, and Fahrenheit selection
persists across reload. Its first harness failure was caused by the temporary
HTML file missing a UTF-8 charset declaration, not by the application.
Twelve additional functional browser scenarios now have passing evidence.
The complete Playwright suite and the requested physical navigation retest
remain distinct outstanding checks; no production-clear claim is made.


Final physical monitoring on `dev-release-tests-20260925`: five Wi-Fi scans and
46 state samples over 117.8 seconds, with monotonically increasing uptime,
unchanged configuration and zero reported refresh/fetch/transport/parse/allocation
errors. A real HTTP 200 forecast refresh completed. Sampled internal free heap
was 57,511–57,811 bytes, ending at 57,803; smallest sampled largest
block was 23,552 bytes. The aggregate historical low watermark was
39 bytes; as explained above, this sums per-region lows and is not an
instantaneous free-heap measurement. Worst-case simultaneous allocation headroom
is still not proven. The physical ten-round touch retest was requested again
on the current build; no user result had arrived at report time.


## Strict quality review and authorized publication

The user explicitly requested the thermo-nuclear code-quality review, fixes
and publication. The structural findings and remedies are recorded in
`2026-09-25-e1003-code-quality.md`. After those changes, all 334 host tests pass
normally and under ASan/UBSan; all 819 web unit tests pass and the web build
succeeds. The release workflow now retains the full sanitizer run as a gate.
The release branch will use the existing pull-request and main deployment
pipeline, including the complete four-shard browser suite. Earlier “nothing
published” statements describe the preceding test stages, not this new request.


### Follow-up PR review

- Preserved a new USB frame's magic bytes when an incomplete preceding header
  is rejected for an impossible length. Added an executable regression.
- Separated overview download and rendering phases. Cache identity is captured
  after this refresh's downloads but before reading row data, preserving the
  concurrent-publication protection. Added immediate prepared-frame reuse coverage.
- The boot background sweep ignores the selected spot, whose refresh belongs
  to the dashboard task. Its completion now describes the same set of spots.
- The failed-prefetch fixture now returns an actual timeout. Documented host
  zlib dependencies, synchronized lockfiles and removed machine-specific paths.
- On the connected quality-test build, the initial HTTP connection failed while
  cached rendering remained valid. A later sample at 714 seconds had monotonically
  increasing uptime, unchanged configuration, 58,359 bytes free heap and no
  current reported errors, but no fetch in that sample. This does not prove a
  successful network recovery. Forced refreshes with a usable cache do not earn
  an extra five-minute retry; scheduled/coverage/initial failures do. The existing
  retry tests cover that policy.


The final review pass makes recoverable UBSan reports fatal in CI, preserves
five-minute prefetch retry deadlines, handles USB bytes resuming during the
expiry poll, and pins touch-column tests to literal display coordinates. A
local sanitizer run was interrupted by host disk exhaustion; after removing
obsolete generated build directories, all 336 tests passed. The subsequent
337-test run additionally uses `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
The CI run remains the publication gate for that final change.


### Final source validation (`00938e8`)

All 337 host tests pass normally and under ASan/UBSan with fatal UBSan reports.
The E1003 build succeeds. The connected device was flashed with
`dev-final2-tests-20260925`; application read-back hash and unchanged OTA selector
were verified, and configuration digest remained unchanged.

Physical USB checks passed: a truncated header followed immediately by hello;
partial-frame recovery after gaps of 3.01, 3.06, 3.13, 3.19 and 3.26 seconds;
replay rejection after an oversized header; a fragmented 16,384-byte request;
and a bad-CRC frame followed by a valid request. The final sample reported
62,559 bytes free heap, a 31,744-byte largest internal block and no current
refresh/transport/parse/allocation errors. No forecast fetch occurred in that
sample, so it is not HTTP-recovery evidence. The final source's automated code
review completed successfully. Only documentation changed after this source
validation; the full release workflow still runs for the final commit.
