# WindScout release checklist

WindScout ships from this monorepo. The production configurator, browser
installer, reTerminal firmware and shared renderer must never be released from
`windscout-site` or assembled by hand from separate builds.

The browser installer supports the Seeed Studio reTerminal E1001, E1002 and
E1003 with one configured spot per device.

## What the release workflow guarantees

`.github/workflows/firmware-release.yml` is the single release pipeline:

1. It verifies the renderer, spot catalog, web unit tests and browser tests.
2. It runs the firmware host tests and installer-bundle tests.
3. It builds the shared E1001/E1002 firmware and the E1003 firmware with ESP-IDF 6.0.2.
4. It creates both the downloadable GitHub Release files and the nested,
   same-origin firmware bundle used by the website from that one build.
5. It places the website bundle in `web/public/firmware`, builds the site and
   checks that every referenced firmware part exists and has the expected size.
6. It prepares a deployable site artifact from `main`. When the repository
   variable `WINDSCOUT_PAGES_ENABLED` is `true`, it also deploys that artifact.
   A `v*` tag publishes the firmware as a GitHub Release.

The browser uses `./firmware/` by default, so the same artifact works on the
custom domain and on the repository's temporary GitHub Pages URL. Do not point
`VITE_FIRMWARE_BASE_URL` directly at GitHub Releases: those downloads are not a
same-origin browser installer host.

## One-time GitHub setup

Before enabling the Pages deployment:

- Deploy the nearby-location Worker first. From `workers/nearby-location`, run
  `npm ci` and `npm run deploy`, then copy its HTTPS URL.
- Add that URL as the repository variable `VITE_NEARBY_LOCATION_URL`. The next
  production build uses it to replace Brouwersdam with the nearest bundled spot.
  The variable is optional: without it the site keeps Brouwersdam and still builds.
- In the `windscout` repository, open **Settings → Pages** and select
  **GitHub Actions** as the publishing source.
- Confirm the account plan permits Pages for this private repository, or make
  the repository public. A Pages website is public even when its source repo is
  private.
- Add the repository variable `WINDSCOUT_PAGES_ENABLED` with value `true` only
  after Pages accepts the repository. Until then, `main` still verifies and
  packages the complete site but deliberately skips publication.
- Keep the custom domain on `windscout-site` until the new Pages deployment has
  passed at its temporary URL.

## Installer diagnostics setup

Installer failures are reported to Sentry only from production builds. This is
not general website monitoring: successful installs, configurator use and
errors outside the installer must not create events.

To enable diagnostics, configure all of these Actions values. A release still
builds without them; diagnostics and source-map upload are then disabled:

- Repository variable `VITE_SENTRY_DSN`: the public browser DSN.
- Repository variables `SENTRY_ORG` and `SENTRY_PROJECT`: both currently use
  `windscout`.
- Actions secret `SENTRY_AUTH_TOKEN`: a Sentry token with release and project
  access. Never put this token in source, logs, screenshots or artifacts.

The Sentry project must keep these defenses enabled:

- Store no visitor IP addresses and apply Sentry's default data scrubber.
- Scrub passwords, passphrases, SSIDs, BSSIDs, authorization values, cookies,
  tokens, API keys, secrets, coordinates, email addresses, IP addresses and
  configuration fields.
- Accept browser events only from `windscout.emieljanson.com` and the temporary
  `emieljanson.github.io` Pages origin.
- Keep spike protection enabled.
- Keep the active `WindScout installer failures` alert. It emails issue owners,
  or recently active project members when no owner exists, for new or regressed
  events tagged `windscout.diagnostic=installer`.

When Sentry is configured, the release build uses the Git commit SHA as the
Sentry release, uploads hidden source maps, and removes every `.map` file before
GitHub Pages packaging. The public DSN may be exposed in the JavaScript bundle;
the auth token may not.

Each failed installer event includes only the bounded, filtered information
needed to reproduce the hardware path:

- selected, detected and release board IDs;
- detected and release firmware versions;
- app or bootloader connection path and install decision;
- current phase, stable error code and serial-command durations;
- bootloader baud-rate fallback and the last flash byte progress;
- explicit `render_failed` or `commit_failed` verification outcomes.

Use `selected_board_id`, `detected_board_id`, `decision_reason`, `phase` and
`error_code` as the first Sentry filters when validating an E1003 remotely.

## Dashboard activity analytics setup

Production firmware sends a personless `windscout_dashboard_heartbeat` event
to the PostHog US ingestion endpoint. Pull-request firmware compiles with
analytics disabled.

Before releasing:

- Add repository variable `WINDSCOUT_POSTHOG_PROJECT_TOKEN` with the public
  PostHog project token. The release fails safely when it is missing.
- In PostHog, enable project-level IP-address discarding before the variable is
  enabled. Treat this as a privacy release gate.
- Save an insight for unique `distinct_id` values of
  `windscout_dashboard_heartbeat` over the last 9 days. Add breakdown views for
  `firmware_version` and `device_type`.

The event contains only its name, random dashboard ID, firmware version,
device type and `$process_person_profile: false`. It contains no location,
Wi-Fi details, configuration, forecast, weather, serial number or hardware
address. The device attempts it at most weekly after a successful forecast
refresh; a failed delivery may retry after 24 hours. Analytics failure never
changes forecast, display or sleep behavior.

This is an approximate fleet signal, not billing-grade data: client events can
be blocked or spoofed. To disable it, clear the repository variable and ship a
new firmware release.

## Moving the production domain

The DNS record already targets `emieljanson.github.io`, so no DNS redesign is
needed. During the release window:

1. Deploy and smoke-test the new site from `windscout`.
2. Remove `windscout.emieljanson.com` from the old `windscout-site` Pages
   settings.
3. Add `windscout.emieljanson.com` to the `windscout` Pages settings and enable
   HTTPS after GitHub validates the domain.
4. Test the configurator and a real USB install on the production HTTPS URL.
5. Archive `windscout-site` only after the production checks pass.

GitHub requires the custom domain to be configured in repository settings; a
checked-in `CNAME` file is not a replacement when Pages is deployed by Actions.

## Automated gates

- `npm test` in `web/`
- `npm run test:e2e` in `web/`
- `npm run build` in `web/` both with and without `VITE_NEARBY_LOCATION_URL`
- `npm run renderer:check` in `web/`
- `npm run spots:catalog:check` in `web/`
- `npm run build` in `web/`; verify `esptool-js` remains in a lazy chunk
- A Sentry-enabled release uploads source maps; every release leaves no `.map`
  file in the deployable site artifact
- `make -C firmware test`
- `python3 firmware/scripts/test_generate_installer_manifest.py`
- E1001/E1002 and E1003 ESP-IDF 6.0.2 release builds
- Immutable installer bundle generated from the same build as the OTA app
- Hash, 32 MB bound, write-range and protected-storage validation

## Physical acceptance matrix

Record date, release version, OS, browser, cable, board serial label, and result
for each run.

- Clean E1002 install on current Chrome/macOS
- Clean E1002 install on current Edge/Windows
- Clean E1002 install on current Firefox/Linux
- Clean E1001 and E1003 installs on a supported desktop browser
- Configuration-only update with no firmware write
- Preserving firmware update with Wi-Fi and configuration retained
- Damaged application repair
- Cancelled device chooser
- Wrong Wi-Fi followed by retry
- Disconnect during configuration; previous setup still boots
- Disconnect during flash; bootloader repair succeeds
- Unsupported non-S3 device; no write occurs
- Unverified compatible S3 with confirmation declined; no write occurs

## Privacy inspection

Use a unique test password, then confirm it appears nowhere in browser storage,
URLs, analytics, network requests other than the active USB exchange, console
output, firmware logs, diagnostics, screenshots, toasts, or test artifacts.
Confirm the password input clears after submission and rejection.

Before enabling a new diagnostics release, create one controlled installer
failure and verify all of the following:

1. The Sonner toast changes from `Sending technical details…` to
   `Technical details sent`, and the same `WS-…` reference stays selectable in
   the recovery screen.
2. Searching Sentry for `windscout.reference:<reference>` finds exactly that
   event. The phase, stable error code and filtered timeline are useful.
3. The event payload contains none of the planted password, SSID, configuration
   values, coordinates, email, IP address, cookies, headers, request body, full
   user agent or URL query values.
4. Blocking `ingest.de.sentry.io` changes the toast to
   `Technical details could not be sent.` without changing recovery controls or
   USB safe-to-disconnect guidance, and no reference is displayed.
5. A complete successful install sends no Sentry request.
6. The deployed site serves no JavaScript source-map file, while the controlled
   Sentry event still resolves to readable production source locations.

Record the tested release, browser and reference in the release notes. Do not
paste the planted password or raw diagnostic payload into those notes.

All three supported models use the website over USB. General availability covers
E1001, E1002 and E1003; record acceptance for the applicable rows above for each
model, not just E1002. For the September 2026 launch, the project owner confirmed
model support and installation-browser coverage. Automated checks do not replace
that physical acceptance.

## Release monitoring and rollback

For the first hour after a production release, one person owns the following
checks:

- The `WindScout release` workflow and Pages deployment remain green.
- The production URL, `firmware/latest.json`, its referenced manifest and every
  firmware part return HTTP 200 over HTTPS.
- A clean browser session can open the configurator and reach the USB device
  chooser without console or network errors.
- Repeat the post-release setup smoke check on E1001, E1002 and E1003: each wakes,
  fetches a forecast, renders the selected spot with the correct local time and
  returns to sleep. Record which models were actually checked.

If the website is broken, redeploy the last known-good commit before changing
the custom domain. If a firmware release is broken, stop the public installer,
restore the last known-good `latest.json` plus its immutable version directory,
and use the USB recovery flow on affected test devices. Keep `windscout-site`
available—but not active on the production domain—until these checks have
passed, so the domain move itself remains reversible.
