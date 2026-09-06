# WindScout USB serial protocol v1

The E1002 USB-C connector exposes its UART bridge. Installer responses are serialized with firmware console output so log bytes can appear between frames, but never inside a CRC-protected frame. Browser parsers ignore console bytes while searching for the next frame magic.

## Frame

Every frame starts with the eight-byte magic `WINDSC01`, followed by little-endian fields: protocol version (`u16`), message type (`u16`), request ID (`u32`), payload length (`u32`), payload CRC32 (`u32`), then a UTF-8 JSON object. Payloads are limited to 4096 bytes. Unknown versions, types, oversized payloads, timeouts, malformed JSON, or bad checksums return a typed error when a request ID can be trusted and otherwise reset the parser silently.

## Commands

| Family | Request | Result |
| --- | --- | --- |
| Identity | `hello` | Board ID, firmware and flash-layout versions, protocol/config ranges, capabilities |
| Hardware | `set_hardware_profile` with `hardwareModel` and `expectedRevision` | Persists the selected E1001/E1002 profile and reports its new revision |
| State | `get_state` | Configuration digest, Wi-Fi health, last render status, and asynchronous apply status; never credentials |
| Session | `begin` with browser `unixTime` | Sets the system clock and battery-backed RTC before configuration or network work |
| Wi-Fi | `scan_networks` | Deduplicated SSIDs with signal/security metadata |
| Wi-Fi | `test_wifi` | Write-only `ssid` and `password`; returns status only |
| Configuration | `stage_configuration` | Validates one configuration candidate and returns its digest |
| Configuration | `apply_configuration` | Starts the candidate render and returns `applying` without blocking the serial service |
| Verification | `get_state` | Poll until apply is complete and Wi-Fi, accepted digest, and render status all verify |

Each request receives one result or typed error with the same request ID. The browser times out an idle request after 15 seconds and network testing after 45 seconds. Forecast/render work runs asynchronously; the browser polls state once per second for at most three minutes. A reconnect starts a new session and request-ID space.

`unixTime` is a required integer Unix timestamp in seconds, captured from the owner's browser immediately before `begin`. Firmware with the required `clock-sync` capability accepts dates from 2025 through 2099 and returns `clock_rejected` without starting the session when it cannot persist that time. The absolute clock comes from the browser. The last successful forecast time uses the installed device's IANA timezone. Forecast and wake times use the installed spot's IANA timezone.

## Hardware profile capability

Universal E100x firmware advertises `hardware-profile` in `hello`. Its hello result adds:

- `hardwareModel`: the model whose display driver is active in this boot: `e1001`, `e1002`, or `unknown`.
- `storedHardwareModel`: the persisted `e1001` or `e1002` selection, or `unknown` before selection, even when recovery has disabled a stored driver for the current boot.
- `hardwareProfileRevision`: the current unsigned profile revision used for optimistic concurrency.
- `safeBootOverride` and `driverFailureLatched`: recovery flags. When either is true, `hardwareModel` is `unknown` and display-affecting setup remains blocked.

To select a profile, send `{"command":"set_hardware_profile","hardwareModel":"e1001","expectedRevision":4}` (or `e1002`) using the revision from `hello`. A successful new selection returns `reboot_required` and the committed `hardwareProfileRevision`; the browser must reconnect after reboot and confirm the selected model is active. An idempotent selection may return `hardware_profile_saved` with the current revision.

The command and setup guard use these typed errors:

- `hardware_profile_unsupported`: this firmware does not provide profile selection.
- `hardware_profile_rejected`: the request has unknown fields, an invalid model, or an invalid revision.
- `hardware_profile_conflict`: the expected revision is stale or the requested transition is not allowed.
- `hardware_profile_save_failed`: persistence failed.
- `hardware_profile_required`: `scan_networks`, `stage_configuration`, `test_wifi`, or `apply_configuration` was requested without an active E1001/E1002 profile, including safe-boot and driver-failure recovery boots. No setup mutation is performed.

## Redaction and compatibility

`password` is write-only. It is forbidden in hello/state/status results, diagnostics, logs, errors, screenshots, and toasts. Implementations must reject unknown fields on credential-bearing requests. Capability negotiation, not firmware-version guessing, decides which optional commands are available. The flash-layout version decides whether an update may preserve partitions or requires a clean reinstall; firmware predating this field is layout 1. Protocol v1 supports configuration schema v4. A configuration requires `deviceTimezone` plus the spot timezone. Universal E1001/E1002 firmware uses release and configuration board ID `seeedstudio_reterminal_e1002`; E1001 remains a hardware-profile identity. E1003 firmware uses `seeedstudio_reterminal_e1003`.
