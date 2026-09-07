# Codebase simplification — 2026-09-05

Scope: repository-wide structural and usage review of the configurator, weather
and spot data, installer, shared renderer, firmware runtime, board selection,
legacy photo-frame application, build tools and release workflow. This extends
the earlier review of working-tree changes. Generated assets, vendored libraries
and design studies were inspected as dependencies, not rewritten or deleted.
This is a targeted engineering review, not a line-by-line certification.

The product needs to configure a device, install it reliably, and display a
trustworthy forecast with minimal battery use. Code earns its place by supporting
those paths, recovery, or a build target the repository still exposes.

## Changes

- Deleted `firmware/main/png_decoder.c` and its header and CMake entry. Its only
  exported function had no caller, including in the legacy firmware. The active
  PNG/image pipeline in `image_processor.c` remains covered by host tests.
- WindScout E1002, universal E1001/E1002 and E1003 builds now default to the
  firmware step. Their CMake branch embeds neither the photo-frame webapp nor
  generated splash screens. Explicit `--step` requests and legacy board defaults
  retain their behavior.
- Fixed the adjacent missing-SDK error handler, which referenced an unbound
  exception instead of reporting why the build could not start. Removed a
  redundant `shutil` import and duplicate CMake test definition.
- Removed the unused scene `resetView` function and write-only
  `tideRequestInFlight` state. Request IDs and stale-response protection remain.
- Removed unused single-forecast-write and cache-clear APIs. Cache tests now use
  the batch API that production uses, including a check that an invalid batch
  cannot overwrite stored data.
- Collapsed the stand-hiding wrapper into `hideDeviceStand(scene)`, updating its
  production, test and prototype callers. Hidden geometry remains attached for
  resource disposal.
- Updated the root build example to the universal installer target and replaced
  obsolete WindScout Wi-Fi OTA instructions with the actual USB update path.

Review categories: one reuse improvement, five deletion/quality improvements,
one removal of unnecessary build work, and one adjacent error-handler fix.
The documentation corrections are additional to those code findings.

## Deliberately retained

- Legacy photo-frame UI, CLI, OTA services and drivers: still referenced by
  selectable board builds. Removing those products is a support decision.
- Persisted cache/configuration migrations and older firmware recognition:
  required to preserve installed devices and safe updates.
- Separate forecast and tide clients/caches: different validation, identities,
  failure semantics and optional-data behavior. A generic framework would add
  indirection without deleting the domain rules.
- Installer cancellation, diagnostic redaction, serial timeouts and recovery
  guards: these handle actual failure paths, not speculative extensibility.
- Remaining single-spot/multi-spot firmware scaffolding: there is further room
  to simplify `wind_app.c`, but its ESP-only button, cache and locking integration
  is not exercised by the host suite. No claim of hardware validation is made.
- Model provenance validation and renderer bridge helpers used by verification
  are not dead merely because production JavaScript does not call them.

## Verification

- Web: 449 tests passed across 48 files, including native/WASM fixture parity.
- Firmware: `make test` built host targets and passed all 241 registered tests.
- Build/bundle tools: 11 Python tests passed, covering the new default steps,
  explicit overrides, legacy defaults and missing-SDK failure.
- Production web build passed; existing large-bundle warning remains.
- `git diff --check` passed. No central lint/typecheck or primary configurator
  lint/typecheck script is configured. The unchanged legacy webapp's separate
  lint task was not run.
- No hardware flash, device firmware build, physical-device test or deployment.

Other work modified the landing page, routing and scene during this pass. Those
edits were preserved and are not attributed to this review. Test counts describe
the workspace when each check ran, not future concurrent edits. The three skill
review rubrics were applied sequentially in the main task per user instructions.
