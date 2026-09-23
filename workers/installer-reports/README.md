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
