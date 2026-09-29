# Installer device evidence

## What the report preserves

USB timeouts previously recorded byte counts but discarded the console bytes containing ESP32 crash details. The installer now recognizes fixed crash signatures and retains only numeric reset reasons, code addresses, an ELF hash, and numeric checkpoints. It never uploads arbitrary console lines, register contents, stack contents, Wi-Fi names/passwords, or location/configuration contents.

Crash evidence is separate from the bounded command timeline. State polling replaces the latest checkpoint rather than evicting a crash. Repeated identical reset reasons and ELF hashes are deduplicated so normal reconnect boots cannot displace a crash. Console bytes already buffered after a response are decoded before closing the port. Evidence is retained while credentials temporarily prevent sending; the report is created after the credential lock is released. Completion/cancellation clears it. A failed Sentry delivery exposes a downloadable JSON report on the error, reconnect, and Wi-Fi screens. Download it before closing or refreshing the installer.

`hello` records the actual running firmware before the first `get_state`, including when that request fails or the firmware has just been upgraded. Existing firmware remains compatible: health fields are optional, and its existing panic output can still be decoded.

## USB request evidence

USB request failures also retain a fixed `failure.stage` (`encode`, `write`,
`read`, `response`, `read-ended`, or `timeout`) and an allowlisted error name,
including while credentials suppress free-form text. Unknown names and arbitrary
error messages/stacks are excluded from this structured field. `idleMs` measures
the wall-clock gap since opening the protocol or finishing its previous request;
it does not establish whether the computer or device slept. These fields survive
the collector and Sentry filters and appear in the downloadable report. They do
not change retries, timeouts, connection recovery, or firmware sleep behavior.

The September 28 WINDPEEK-D reports lack this evidence. Their USB failure cannot
be attributed retrospectively to device sleep, a disconnected cable, or a host
driver. Validate on hardware with a 40-minute pause at Wi-Fi entry, followed by
setup submission; separately exercise host sleep and USB disconnection. Unit
tests simulate stream failures and elapsed time, not actual device power policy.

## Numeric checkpoints

| Stage | Operation reached |
| --- | --- |
| 0 | Installer initialized |
| 1 | Wi-Fi connection starting |
| 2 | Wi-Fi connection returned |
| 3 | Forecast preview starting |
| 4 | Forecast preview returned |
| 5 | Activating the candidate |
| 6 | Persisting the candidate |
| 7 | Reloading saved spots |
| 8 | Apply completed successfully |
| 9 | Installer idle timeout |

A stage marks progress, not success; inspect `apply` and `applyError` too. `freeHeap`/`minimumHeap` count internal 8-bit-capable heap, excluding PSRAM. `taskStackFree` is the responding UART task's stack high-water mark, in bytes. Console checkpoints measure the task that emits them (Wi-Fi/idle: UART; preview/commit: apply). `uptimeMs` helps distinguish a reboot from an unresponsive command and wraps after about 49 days.

## Hardware idle check, 29 September 2026

- E1002 on `dev-bfdc6973`, connected through Dia/Web Serial on macOS using the
  production protocol adapter plus the local diagnostic changes. No firmware
  flashing or stored Wi-Fi/configuration changes.
- After `hello`, `get_state`, and a successful network scan, no protocol requests
  or reads were made for 2,400,006 ms (40 minutes). macOS display and system sleep
  were inhibited with `caffeinate` for this test.
- `begin` then returned `ready` in 271 ms; `get_state` returned in 343 ms.
  Uptime advanced from 1,648 to 2,405,457 ms, with reset reason unchanged at 1.
  Final state reported connected Wi-Fi, valid rendering, and zero apply errors.
  The probe sent `cancel` and closed the port afterward.
- This is not an exact reproduction of WINDPEEK-D: the September 28 customer
  used Windows/Chrome, `dev-0d4b02dc`, and had no configured Wi-Fi. This device
  already had saved Wi-Fi. No new credential submission or full installation was
  performed. Host sleep, cable disconnection, and a fresh-device 40-minute pause
  remain untested in this run. No underlying cause or behavioral fix is claimed.
- The diagnostic error-name allowlist includes the Web Serial specification's
  `BufferOverrunError`, `BreakError`, `FramingError`, and `ParityError`, in addition
  to general DOM/JavaScript errors. These are evidence categories, not diagnoses
  of the historical incident. See https://wicg.github.io/serial/.

## Matching a crash to source

Download `windpeek-firmware-debug` from the release workflow that produced the device's firmware. Select `e1002` (also E1001) or `e1003`, and match the panic's `elf` prefix to the archived ELF SHA256 before decoding addresses. The artifact contains the exact application binary, ELF, linker map, SDK configuration and checksums for both builds, including production cache reuse. It is separate from the website bundle. GitHub retains it for 90 days; archive the matching artifact before expiry for an ongoing investigation.

Run `sha256sum -c SHA256SUMS` inside the chosen artifact board directory to verify its files.

Use the matching ESP-IDF toolchain's `xtensa-esp32s3-elf-addr2line -pfiaC -e windpeek.elf <addresses>`. Sentry stores addresses as integers; convert them to hexadecimal. Do not substitute a later rebuild just because its version label matches.

## Hardware verification, 19–20 September 2026

Connected E1002, with its original application/settings backed up before testing:

- Official `dev-5f0b6f18`: upgraded from `dev-a5e2c3b` through Dia/Web Serial and reached installation success. A separate ten-minute idle test then completed `begin` and `get_state` in under 0.31 seconds each.
- Instrumented build: three Falmouth, MA applications with saved Wi-Fi all completed with matching configuration digest, connected Wi-Fi and valid rendering (82 protocol responses, maximum 0.586 seconds).
- Fresh settings: erased only the backed-up NVS region, selected E1002, connected Wi-Fi again, and applied Falmouth successfully (69 responses, maximum 1.863 seconds). Rendering took about 85 seconds; polling remained responsive.
- Fault injection: a temporary, unshipped `abort()` in `physical_health` produced an actual device panic. Replayed the captured bytes in 13-byte fragments through the production serial decoder, diagnostics collector and real Sentry SDK/network transport. Read the received event back through Sentry MCP. It retained the abort PC, ten-frame backtrace, ELF prefix `5a767ab23` and reset reason 12. The matching archived ELF resolved the abort to the injected `physical_health` line, followed by `handle_state`. The injection was removed before the successful hardware tests.
- Sentry proof: [WINDPEEK-7](https://windscout.sentry.io/issues/WINDPEEK-7), event `714b55cdcd574aa089459c148fd0091c`, reference `WS-2G4H86G108`, environment `diagnostic-self-test`. An HTTP success response alone was not treated as proof: the stored event was inspected.
- Download fallback: mounted the actual installer Wi-Fi recovery UI in Dia with failed delivery, downloaded the report, and verified the resulting JSON contents. Automated tests also cover error and reconnect screens.
- Original application and settings restored and identified successfully after the destructive test fixtures.

## Fresh-device idle regression

A fresh E1002 without saved Wi-Fi becomes unresponsive after scanning networks and waiting longer than 120 seconds. The installer timeout releases its wake lock; `power_manager_set_installer_active(false)` then re-enabled automatic light sleep even while USB was powered. USB already prevented deep sleep, but the UART could not reliably wake the chip from light sleep. A three-minute hardware reproduction recorded stage 9, light sleep becoming enabled, and four subsequent `begin` requests receiving no bytes.

After the fix, the same fresh device waited ten minutes without requests. All four `begin`/`get_state` pairs succeeded in 0.271–0.287 seconds, with stage 9 and uptime over 600 seconds confirming the timeout occurred without a reboot.

A subsequent fresh Falmouth setup completed with the expected digest, Wi-Fi connected and rendering valid. Five more state requests after completion all returned in 0.309–0.310 seconds, covering wake-lock release at the end of apply.

At boot and installer wake-lock release, the fix leaves automatic light sleep disabled when the board detects USB power. Opening the installer explicitly resets the E1002 through its UART bridge before `hello`, so plugging in a previously battery-powered device re-evaluates that policy. The credential timeout still clears staged credentials, and battery operation retains automatic light sleep and scheduled deep sleep. Boards that cannot detect their USB supply are outside this physical verification; the connected E1002 uses its SY6974B power-good signal.

To repeat the hardware regression, back up settings first, use a fresh NVS configuration, select E1002, send `hello`, `get_state`, and `scan_networks`, then send no commands for ten minutes. Send `begin` and `get_state` four times and require valid responses. Complete a fresh Falmouth setup and require a matching digest, valid render, connected Wi-Fi and responsive state polling after completion. Restore the original settings afterward. This relies on actual ESP32 power management and UART behavior; a source-text assertion or a mocked host test would not prove the fix.

This trigger matches the long idle before the customer's September 18 timeout, but the old report discarded the device console, so that incident's complete causal chain cannot be proven retrospectively. The later apply timeout and the update-frequency report remain unconfirmed. The verified diagnostic path and installation scenarios do not establish that every hardware, cable, Windows driver, network or update-frequency fault is resolved. A complete USB/power loss can prevent the device from transmitting a crash at all. Do not label the original Sentry incident resolved based on this test.
