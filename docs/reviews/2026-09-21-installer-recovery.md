# Installer recovery review — 2026-09-21

Scope: fixes on deployed `a4a6a363`, isolated from the user's other checkout.
Related: https://windscout.sentry.io/issues/WINDPEEK-8

## Diagnosis and policy

The post-update branch for matching configuration and connected Wi-Fi only polled
for a valid render. It never initiated recovery, while the installer wake lock
suppressed scheduled refreshes. The local reproduction made 61 state requests
and zero applies before the same verification error. Boot's initiating failure
cannot be attributed from the old event: its forecast diagnostics only described
USB applies.

Every firmware flash now uses the clean bundle, including updates. This costs
another Wi-Fi setup and intentionally discards stored settings/cache. A current
firmware configuration change still avoids an unnecessary flash, but always
executes the fresh-fetch/render/commit transaction. Successful in-progress applies
can be observed after reconnect without restarting them.

## Review findings addressed

- Removed the passive verification branch and immediate current-setup success.
  One setup transaction owns apply verification, bounded recovery and credentials.
- A recognized app with unreadable settings can be cleanly repaired without
  depending on its failing state handler. Unknown hardware still needs confirmation.
- Reconnect checks model, firmware, configuration version and flash layout before
  mutations. The ROM chip is checked again before erasing. Profile selection has
  a bounded reboot path and must be verified after restart.
- Transient transport/408/5xx failures get one complete retry; rate limits, parsing,
  allocation, oversize responses and commit failures are not blindly retried.
- A capability-negotiated completion acknowledgement keeps the UART awake and
  initial boot waiting until the browser has verified the committed forecast.
  Browsers that do not opt in keep the old completion behavior. A lost cleanup
  reply does not erase already established proof of installation success. A
  completed apply with no browser activity releases the lock after two minutes;
  this timeout never applies to a staged setup waiting for human input.
- Runtime refresh status is published independently of USB apply diagnostics,
  through short critical sections. Render confirmation is atomic across tasks.
  The first failure survives timeline eviction and is filtered again at Sentry.
- Deadline expiry after browser suspension still permits one final state read.
  Cancellation during retry cannot issue another apply. Errors offer a retry.
- PR review tightened reconnect recovery: non-retryable apply failures stay
  visible, transient failures consume the existing retry budget, and a lost
  hardware-profile reply can be retried only while the model is still unknown.
- Overview rendering retains forecast failures, including prefetch failures on
  cached rows. Numeric diagnostic stages have explicit wire values. Completion
  cleanup tolerates an absent apply state, and credential-clear tests stage real
  test credentials before checking cleanup. Shared protocol mocks reject unknown
  commands. Setup and release documentation describe clean updates consistently.
- Extracted the transaction from the session and shared test fixtures; dedicated
  recovery tests avoid growing the existing session test file. No new generic
  framework or per-model installation implementation was introduced.

The thermo-nuclear, reuse, correctness, concurrency, API compatibility, privacy
and test-coverage passes were run sequentially, per the user's workspace policy.
No remaining blocking findings were retained. This is a manual review, not an
independent peer review. Existing giant runtime/session test files were not
expanded into new responsibilities; the transaction and status store are separate.

## Validation

- 729 web tests; 227 installer tests including 44 recovery tests.
- Stateful clean-install scenarios for E1001/E1002/E1003: five-hour Wi-Fi delay,
  wrong password, transient and persistent forecast failure, failed commit.
- Root-cause regression, unreadable state recovery, wrong model/version/chip,
  lost acknowledgement, running apply reconnect, reboot loop, browser suspension,
  cancellation, diagnostic retention and privacy.
- 343 firmware host tests, including capability negotiation and runtime failure
  reporting. Existing forecast-cycle tests require fetched data and valid output
  before setup commits.
- E1001/E1002 and E1003 ESP-IDF builds passed. UART get-state/migration estimate:
  10,256 bytes against a 12,288-byte budget plus 4,096-byte reserve.
- Production web build and diff whitespace checks passed. This project has no
  separate lint/typecheck scripts in its web package.

Physical devices and Windows USB behavior were not retested in this change.
The stateful browser-session tests simulate hardware/network faults; they do not
prove every cable, driver or provider failure is eliminated. No physical device
was flashed, and no Mac wake inhibition was started.
