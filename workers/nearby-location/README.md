# Windpeek nearby-location Worker

This small Cloudflare Worker turns Cloudflare's approximate request location
into latitude, longitude, and country code. Windpeek uses these to choose its
nearest bundled spot and an initial temperature unit. It does not use browser GPS, store coordinates, or log location data.

## Deploy

1. Install the Worker dependencies with `npm ci`.
2. Authenticate Wrangler with `npx wrangler login` when needed.
3. Deploy with `npm run deploy`.
4. Copy the deployed HTTPS URL into the GitHub Actions repository variable
   `VITE_NEARBY_LOCATION_URL`.
5. Run the website release after that variable is set.

Deploy the Worker before the static site. The website intentionally treats the
endpoint as optional: a missing URL, failed response, or unavailable location
quietly keeps Brouwersdam as the default.

## Local use

Run `npm run dev` in this directory. To use that Worker from a local website
build, set `VITE_NEARBY_LOCATION_URL` to the Wrangler development URL before
starting Vite.

The browser acceptance suite uses its own intercepted
`/__windpeek-location` endpoint and never calls the deployed Worker.
