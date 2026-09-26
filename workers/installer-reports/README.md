# Installer report relay

Forwards privacy-filtered installer reports and their JSON attachment to the
Windpeek Sentry project. The browser uses this endpoint because content blockers
can block Sentry's ingestion domain. No request bodies or invocation logs are
stored by this Worker.

## Deploy

Run `npm ci` and `npm run deploy` here, using the existing Cloudflare account.
Set the GitHub repository variable `VITE_INSTALLER_REPORT_URL` to the deployed
Worker URL plus `/report`, then deploy the website. `VITE_SENTRY_DSN` remains
required. The relay validates that DSN against the fixed Windpeek project.

Current endpoint:
`https://windpeek-installer-reports.windscout-nearby-location.workers.dev/report`

Requests are limited to 2 MiB to fit the full diagnostic attachment and event. Only the Windpeek website and local browser QA
origins are accepted. This is a public ingestion endpoint; origin checks are
not authentication. Cookies, authorization and client IP headers are not
forwarded. The upstream timeout is four seconds, and upstream failure or rate
limits remain failures to the client. Redirects are never followed.

## Delivery behavior

The installer attaches the same sanitized JSON document offered by Download
report. It retains up to ten failed reports locally for seven days, retries
while the site is open, on connectivity returning, and on subsequent visits.
Storage restrictions or clearing browser data can prevent persistence. A Sentry
HTTP success is required before showing Report sent; it is not a guarantee of
indefinite Sentry retention. Download and email remain available as a fallback.

For a smoke test, send a synthetic report from Dia with blocking enabled and
check both the event and `windpeek-diagnostic.json` attachment in Sentry.

## Telegram project bot

`/stats` shows the last seven days; `/help` explains the bot. The webhook accepts
only the configured private chat and sender, authenticated with Telegram's secret
header. Every Monday at 08:00 UTC (09:00 winter / 10:00 summer in Amsterdam), the
Worker sends a summary. Week comparisons start after fourteen complete days.

Successful installs, reinstalls and firmware updates send distinct messages after
flashing **and** device verification. Settings, already-current devices, failures
and demo sessions do not emit successes. Milestones celebrate 10, 25, 50, 100,
250, 500 and 1000 completed installation sessions. Three error reports in a rolling
hour trigger one warning, with a full-hour cooldown. These are submitted reports,
not a measurement of every error or unique users/devices.

`/visit` counts anonymous tab sessions, at most once per thirty minutes on page
load. It respects Do Not Track and Global Privacy Control. Session storage holds
only a timestamp. No cookies, persistent visitor identifiers, URLs, names, Wi-Fi
or location details are collected. An IP is used only by Cloudflare's ephemeral
rate limiter, not stored in the ledger. Counts are approximate and may be reduced
by blockers, storage restrictions and network failures.

A single SQLite Durable Object owns counts, deduplication and notifications.
Event insertion and lifetime installation totals are atomic. Weekly cleanup removes
events and message reservations older than 21 days (up to 28 days between runs).
Only random event IDs, action categories and timestamps are retained; aggregate
installation totals and the measurement start persist. The legacy per-event
namespace remains deployed for existing cleanup alarms.

Delivery is best effort and at most one attempt per event/update/week. A reservation
is written before contacting Telegram: ambiguous responses are not retried because
Telegram provides no idempotency key. Counts survive a failed notification. The
public ingestion routes cannot authenticate hardware installs; Origin can be forged.
Success requests and failure counting each have an independent ten/minute limit
per Cloudflare location; visits are limited to sixty
per IP/minute. Local QA never contributes to owner statistics. The existing Sentry
relay remains independent of bot delivery failures.

### Configuration

Store secrets through Wrangler, never GitHub files, Vite variables or chat:

```sh
npx wrangler secret put TELEGRAM_BOT_TOKEN
npx wrangler secret put TELEGRAM_CHAT_ID
npx wrangler secret put TELEGRAM_WEBHOOK_SECRET
npm run deploy
```

Configure Telegram `setWebhook` with the Worker URL plus `/telegram`, the same
`secret_token`, and `allowed_updates: ["message"]`. The owner ID is the private
`message.chat.id` from your own `/start` message in `getUpdates` before setting the
webhook. Configure `setMyCommands` with `stats` and `help`.

Deploy the website too: it reuses `VITE_INSTALLER_REPORT_URL`. No new public
configuration is needed. To disable outgoing notifications, delete the bot-token
secret; website and installer operations remain unaffected.

### Verification

Run `npm test` here: it builds and executes the Worker in real local SQLite Durable
Objects, mocking only external Telegram delivery. Tests cover concurrent duplicate
events, owner authentication, failed delivery, visits, milestones and weekly/error
aggregation. Run the web suite for session lifecycle and request validation.
After deployment, request `/stats` in the owner's private chat and complete one
real hardware installation to verify the complete path. Never seed fake installs
in the production ledger.
