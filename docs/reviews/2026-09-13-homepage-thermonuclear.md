# Homepage performance and Thermo-Nuclear review

Reviewed and implemented against the existing dirty `feat/e1003-multiple-spots`
workspace. Scope: homepage startup, shared web configuration boundaries, forecast
publication state and their tests. Existing firmware and other product changes
were preserved. This is not a full firmware or physical-device certification.

Installed the official Cursor `thermo-nuclear-code-quality-review` skill into
`~/.codex/skills/thermo-nuclear-code-quality-review`. Applied its structural review
and the ce-simplify-code reuse, quality and efficiency rubrics sequentially in
this task, following the workspace's agent instructions.

## Findings resolved

1. **Homepage startup depended on the whole configurator.** `LandingHero.vue`
   now owns only the responsive photo and link. Once the photo loads, an idle
   callback loads `LandingForecast.vue`; browsers without idle callbacks use a
   timer. Cached and failed images are handled too. The live nearby forecast,
   saved preferences, swell example fallback and calibrated visual treatment
   remain. Navigation cancels scheduled work; a failed import leaves the photo.
2. **Configuration serialization copied unrelated runtime state.**
   `config/spotSettings.js` provides the canonical display-field snapshot used by
   spot switching, installation and share links. Serializers no longer spread
   the Pinia store and evaluate every runtime getter for each extra spot.
   Active-spot tide availability and inactive-spot preferences retain their
   previous treatment.
3. **Pending preview publication was four independently mutable fields.**
   The store now holds one nullable `pendingForecast` object. The existing
   `pendingForecastRevision` getter keeps the scene's read interface stable.
   Publication and rollback still reject stale spot/model/revision combinations.
4. **Every forecast mutation rebuilt the shared URL.** URL synchronization now
   watches its actual configuration dependencies, rather than subscribing to
   every mutation in the store. Thresholds, spot lists and saved display changes
   still update the URL; forecast status messages no longer trigger that work.
5. **Late hero redraw failures escaped cleanup.** The live canvas has one
   idempotent disposal path for initial failures, later redraw failures and
   unmounting. Failed rendering returns to the product photo.
6. **A null additional spot crashed validation.** Multi-spot validation now
   rejects malformed entries without throwing. The regression test includes
   null, undefined, scalar and empty-object entries.
7. **The comparison test omitted the new hourly-detail row.** Updated the
   expected label and values to the intended E1003 comparison; kept the rest of
   the assertions.

## Measured result

Production build manifests were traversed through static imports starting at
the app entry and landing page. Deferred live-forecast imports were excluded in
both measurements; before this change those dependencies were static. Gzip sizes
are the sum of individually compressed emitted JavaScript chunks.

| Initial homepage JavaScript | Before | After |
|---|---:|---:|
| Minified bytes | 1,656,960 | 160,527 |
| Gzip bytes | 420,710 | 60,228 |

That is **85.7% less compressed JavaScript before the live enhancement**. The
remaining live forecast still downloads its dependencies later; this is not an
86% reduction in total session traffic or a measured page-load-time improvement.
Images, fonts, CSS and WASM are outside this JavaScript comparison.

## Verification

- Full web suite: 651 tests passed across 67 files.
- Final store refinement: 59 affected tests passed again.
- Seven homepage/nearby-location browser tests passed, including a deliberately
  delayed hero image proving the heading/link appear before forecast dependencies.
- Six additional browser flows passed: changing spot and threshold, compact
  controls, optional forecast rows, independent E1003 spot settings and the fake
  E1002 installation through reconnect, Wi-Fi and completion.
- Production build succeeded. Its large-chunk warning remains for deferred
  configurator, map and 3D code.
- Production wind and `/swell/` pages inspected in the browser; photo framing and
  the live wind/swell canvas remain aligned.
- `git diff --check` passed.
- No lint or typecheck command is configured in the web package.
- No firmware edits, device flash, deployment, commit or push in this pass.

## Deliberately retained

- Separate wind, tide and swell request handling: they have different failure
  and availability semantics; a generic request framework would obscure them.
- Installer cancellation, timeout, checksum and recovery safeguards.
- Existing large firmware runtime/renderer modules: extracting ESP-specific
  locking and hardware state without device validation would exceed this web
  pass. Their size alone is not evidence that arbitrary splitting helps.
- Existing marketing layout and battery claims: this pass improves performance
  and code. The separate physical battery validation recommendation still applies.

## Second pass: installer verification and persistence

Three further findings were reproduced by four failing regression tests before
changing production code:

1. **False success for an unchanged configuration with an invalid screen.**
   The `UP_TO_DATE` branch skipped render verification. It now reuses
   `verifyCurrentConfiguration`, waiting for pending rendering and surfacing
   `render_failed` instead of claiming completion. It does not reapply or flash
   an otherwise current device. A current-attempt check prevents completion after
   cancellation.
2. **Additional spots escaped diagnostic text redaction.** Installer setup now
   registers ID, name and timezone for every spot, using the existing diagnostic
   redaction mechanism. A real diagnostic snapshot test proves extra-spot text
   is removed. This demonstrates a protection gap, not evidence that user data
   was actually transmitted.
3. **Forecast activity rewrote local preferences.** Persistence now watches the
   explicit preference fields, including nested per-spot choices, instead of
   every store mutation. Writes remain synchronous and storage failures remain
   non-blocking. Tests confirm runtime forecast updates cause no storage writes
   while nested spot edits are saved.

Full web suite after these changes: 655 tests passed across 67 files. Production
build and `git diff --check` passed. Three affected browser flows passed: E1003
spot settings, E1002 installation and diagnostic-reference recovery. No additional
firmware or UI layout changes.

## Third pass: shared setups and personal-spot imports

Two further findings were reproduced with failing regression tests:

1. **Old spot settings survived opening a different shared setup.** Pinia's
   object patch deep-merged the previous `spotSettings` dictionary. Re-adding
   an old spot could restore its obsolete threshold. Applying a shared setup
   now replaces the dictionary through a function patch. The regression covers
   opening the new setup and subsequently adding the old spot again.
2. **Ten personal spots caused eleven storage writes.** Each imported spot
   reread and rewrote storage; the active spot was imported twice. The shared
   link importer now deduplicates its active spot and uses one bulk operation.
   The existing single-spot API delegates to the same implementation. Existing
   saved spots are retained, matching IDs updated, invalid batches rejected
   before writing, and unavailable storage remains non-blocking.

Verification: 659 web tests passed across 67 files, including four new regression
tests. Two browser flows passed: creating and remembering a personal spot, and
adding E1003 spots while preserving individual settings. Production build and
`git diff --check` passed. The existing deferred-chunk size warning remains.
No commit, push or deployment performed.

## Sentry alert follow-up: exclude local previews and automated browsers

The September 13 alert email links to Sentry issues 146758144 and 146757782.
Their event details require a login, so their precise origin remains unconfirmed.
The message alone does not distinguish rejected apply, render failure or commit
failure. No installer success/failure checks were weakened to suppress it.

A separate, reproducible reporting gap was confirmed: the default reporter
checked only Vite's `PROD` flag. Local production previews and automated browsers
could therefore submit test failures under the production environment. Five
regression cases failed before the fix using a fake transport (no live reports).
Default reporting now excludes localhost/subdomains, IPv4 loopback, IPv6 loopback
and browsers exposing `navigator.webdriver`. A positive regression verifies that
ordinary production browsers still send reports. Explicit injected reporters
remain available for isolated SDK tests.

The small implementation was simplified and manually reviewed within its two
changed files, without expanding review into unrelated branch work. This closes
the test-reporting gap; it does not prove these two historical alerts were tests.
