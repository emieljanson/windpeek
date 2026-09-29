# Automatic support reports

The downloadable `windpeek-diagnostic.json` and the JSON attached to automatic
installer failure reports in Sentry share the same allowlisted `support` data.
Sentry also receives this data in `extra.support`; queued reports retain it.
Reports can be downloaded after successful setup too. Normal device use does
not upload the journal: the browser reads it when the owner connects by USB.

## Included information

- Browser capture/start time (Unix milliseconds), firmware, detected hardware
  model and a random installation identifier (`deviceId`, 32 hexadecimal digits).
- Selected and installed settings: spot names/coordinates/timezones, forecast
  and swell models, device timezone, module sizes/order/visibility, threshold,
  clock format, temperature units, footer and configuration digest/generation.
  All ten spots are included on E1003.
- The most recent 32 boot, setup-start, setup-completion/failure and refresh
  completion/failure events, retained across power cycles in persistent storage.
- Per-event firmware, sequence, device time (Unix seconds), uptime, reset reason,
  wake-reason bitmask, battery percentage, USB/Wi-Fi state, diagnostic stage,
  result, HTTP/transport/parse failures and panel phase/wait duration. Refresh
  events distinguish a failed fetch from a successful cached render.
- Setup and refresh events include their settings. Setup failures are recorded
  before rollback changes Wi-Fi or the active configuration.

Wi-Fi names/passwords, network scan results and arbitrary configuration fields
are excluded. Firmware serializes settings separately from credential storage;
the browser allowlists data again for downloads, Sentry and the retry queue.
The installer explains the included locations, settings and history before
connecting. Existing credential-lock protections still apply.

## USB protocol

Firmware advertises optional capability `diagnostics` in `hello`.
`get_diagnostics` is read-only, including while an apply task is running.

- `{"command":"get_diagnostics","sequence":0}` returns `status`, `deviceId`,
  `firmwareVersion`, `oldestSequence`, `newestSequence`, `storageErrors`,
  `configurationInstalled` and the installed `configuration` when present.
- A positive sequence returns `{"status":"ok","entry":{...}}`, or
  `{"status":"missing"}` if that slot has been overwritten or is unreadable.
- Both sequence bounds are zero for an empty journal. Invalid sequences are
  rejected. Unmounted/unavailable persistent storage returns
  `{"status":"diagnostics_unavailable"}`.

Each response fits the existing 16 KiB USB frame. The browser freezes the first
reported range and reads at most 32 events, with a five-second timeout per
request. Missing or unreadable slots are marked `partial`; failed requests or
unavailable storage are marked `unavailable`. Legacy firmware is marked `unsupported`. A history timeout must
not classify the device as damaged or trigger a reinstall.

## Retention and interpretation

The journal uses 32 rotating JSON files, each less than 14,000 bytes, plus a
small identity file. Writes use a temporary file, flush/fsync and rename.
Interrupted temporary files are ignored. Logging errors never replace the
original operation result. Recovery boots that cannot mount persistent storage
cannot supply a journal.

The current installer erases storage on **every firmware change**. It captures
available evidence before flashing and retains it under `beforeFirmwareErase`
in that browser session's report. A new installation gets a new identifier;
the pre-erase and post-erase histories remain separate. Closing the browser
before downloading/reporting can lose that in-memory pre-erase copy. This is
not a cloud backup of every successful installation.

Device timestamps depend on RTC/browser/network clock state and can jump.
Compare sequence numbers and uptime within a boot, and browser capture time,
before drawing conclusions about dates. A `setup-started` event without a
completion before the next boot can indicate interruption, but does not prove
its cause. Older firmware cannot provide retroactive history.

## Validation

Host tests cover persistent identity, credential exclusion, rotation,
interrupted writes, bounded frames, ten-spot settings and cached-render/fetch
failure distinction. Protocol tests cover capability negotiation, read-only
access during apply and invalid sequences. Web tests cover cancellation,
legacy devices, pre-erase retention, report/queue/Sentry parity and history
timeouts without destructive recovery.

Before releasing, verify on hardware: induce one failed setup, reconnect,
confirm the original failure precedes cleanup, power-cycle and confirm retained
events. No physical device was available for this implementation's validation.
