# Spot catalog maintenance

Run commands from `web/`. Pinned snapshots, exclusions and source permissions live in `data/spots/source-manifest.json`. Reviews are in `docs/reviews/2026-09-09-americas-spots.md` and `docs/reviews/2026-09-09-worldwide-spots.md` at the repository root.

## Regenerate from reviewed snapshots

```sh
npm run spots:import
npm run spots:validate
npm run spots:catalog
npm run spots:catalog:check
```

The committed validation cache makes the current regeneration offline. New unresolved points require `VITE_GEOAPIFY_API_KEY` (environment or `.env.local`); the validator enforces `SPOT_VALIDATION_CREDIT_BUDGET` (default 3000).

## Refresh Surf-Forecast metadata

The project owner confirmed permission on 9 September 2026. Keep that permission and attribution in the manifest. This fetch imports names, coordinates and break types worldwide from the public country directory and map, explicitly including the US and Australian province groups.

```sh
node scripts/spots/fetch-surf-forecast.mjs /tmp/surf-forecast-new.json
```

Review changes before replacing the pinned snapshot or updating the manifest path. The script stops on incomplete country discovery, missing spot identities or coordinates and only writes the output after completion. The current snapshot covers every one of the 8,032 directory entries; unlisted or future locations are not implied. Preserve or re-evaluate explicit exclusions when refreshing; the source can retain stale spots and duplicate beach names.

## Duplicate review

```sh
node scripts/spots/review-duplicates.mjs /tmp/duplicate-review.json
```

Equivalent names (case, accents and punctuation normalized) within 5 km are automatically deduplicated. Each suppressed record must directly match a retained winner, so proximity chains cannot merge an entire coastline. Existing source priority determines the winner deterministically.

Names differing only by Praia/Playa/Beach and connecting words are now merged within 3 km (at least four remaining characters). A point and its neighbouring beach stay distinct. Non-surf facilities within 75 m are consolidated into one forecast location. Differently named surf breaks stay separate even when close. Record confirmed Surf-Forecast variants in the manifest's `excludedRecords` with the retained source ID; ambiguous pairs stay separate. Review suggestions may include legacy OSM schools and clubs; they are not all duplicate beaches.

After importing new candidates, optionally populate timezones offline. Install the acquisition-only dependency outside this repository:

```sh
npm install --prefix /tmp/windscout-spot-tools geo-tz@8.1.8
NODE_PATH=/tmp/windscout-spot-tools/node_modules node scripts/spots/cache-surf-forecast-timezones.mjs
```

Then run validation and catalog generation. Country labels come from the source directory; timezones come from geographic timezone boundaries. This does not independently verify every location. Unresolved cases fall back to Geoapify. Nine reviewed Saint Martin mappings handle the source's obsolete Netherlands Antilles grouping; new unknown entries are left unresolved.

## Refresh OSM Americas candidates

Export this query from Overpass to a JSON file:

```text
[out:json][timeout:160];
nwr["sport"~"(^|;)(surfing|kitesurfing|kiteboarding|kite_surfing|windsurfing|wingfoil|wingfoiling|wing_foil|wing_foiling)(;|$)"](-56,-170,75,-30);
out tags bb;
```

```sh
node scripts/spots/prepare-osm-americas.mjs /tmp/overpass.json /tmp/osm-americas-new.json
```

Inspect selected names, coordinates and exclusions before pinning the new snapshot. The script excludes successfully imported IDs from the original OSM snapshot, derives missing way/relation centers from bounds, and filters facilities and reviewed unsuitable points. It is a candidate filter, not a guarantee that future OSM additions need no review. Preserve ODbL attribution in the manifest and public data-source page.

Only reviewed popular locations belong in `data/spots/popular-spots.json`; these control automatic nearby defaults separately from the full search catalog.

## Runtime forecast-location grouping

After source validation, the catalog builder groups points within 1 km of a retained centre. Built-in and curated IP spots take priority; remaining ties use a shorter name then stable ID. It does not chain nearby points along the coast. Known country and timezone boundaries are respected.

Generated `aliases` support name search and `aliasIds` resolve old saved IDs through `getSpot`. The nearby builder resolves curated aliases to retained IDs. Original source records stay in the pinned snapshots. Regenerate with the normal catalog commands above.

Explicit wider same-destination decisions live in `data/spots/location-merges.json`. These run after automatic grouping and keep search aliases/old IDs. Each decision names the removed and retained IDs; stale IDs and centre distances over 5 km fail the build. This allows reviewed 1–3 km variants without globally merging distinct bays.

## Progressive distance review

```sh
node scripts/spots/audit-distance-rings.mjs /tmp/distance-rings.json 25
```

This read-only scan evaluates every centre pair within the radius, then flags normalized, translated, reordered and misspelled primary/alias names. Review suggestions before editing `data/spots/location-merges.json`; different bays, numbered breaks and generic names are often false positives. The September 9 review stopped at 25 km after 14 further merges, leaving 8,366 locations and all 222 curated IP defaults.

Automatic location grouping stays at 1 km. Explicit reviewed merges default to a 5 km cap. Misplaced pins beyond that require `maxDistanceMeters` (at most 25,000) plus `evidence`. Verified `coordinateCorrections` contain an existing `id`, latitude, longitude and evidence and run before clustering. Preserve raw acquisition snapshots. Regenerate the catalog and nearby index after changes. Review dispositions and the stopping rationale are in `data/spots/sources/distance-ring-review-2026-09-09.json`; the 95% target is not a measured recall claim.
