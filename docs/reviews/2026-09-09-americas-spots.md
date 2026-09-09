# Americas spot expansion — 9 September 2026

## Result

Catalog grows from 1,794 to 4,948 spots (+3,154 net); no existing IDs removed. Nearby defaults grow from 120 to 145. This is a local catalog change, not a deployment.

Surf-Forecast: 3,101 directory-matched records across 42 country/territory groups, including Central America, Caribbean and Hawaii. User explicitly confirmed permission in this task. Only spot metadata is imported, no descriptions, imagery or forecasts. Public lesser-known map entries are marked `hidden` by the provider; this means less prominent map display, not private access.

OSM: 426 raw records, 127 named new/recovered physical spots after review. Surfing and kiteboarding aliases added to normalization. Reviewed supplemental way/relation centers use bounding-box midpoints. Existing global OSM source is unchanged.

After 37 explicit Surf-Forecast exclusions, 5,060 combined candidates produce 4,946 accepted validation records and 4,948 final catalog entries through the existing catalog builder. Automatic validation rejects 113 duplicates and one existing display-name issue. The importer still reports 1,525 failures in the old global OSM snapshot; none are from the new sources.

## Gut check and corrections

- Reviewed all 127 selected OSM names/tags/coordinates. Excluded shops, schools, accommodation, artificial and river-wave facilities, unnamed and overly large features; explicit reasons are stored in the snapshot.
- Broad country-coordinate checks found Tereza at +28.5249 latitude for Brazil, far into the North Atlantic. Excluded instead of guessing a correction.
- Excluded Kapoho Bay, Shacks and three OSM Kapoho reef points after checking the 2018 lava coastline change. Retained Pohoiki: current official visitor information still documents surfing there.
- Excluded remote Cortez Bank, whose offshore timezone fallback was inconsistent.
- Reviewed cross-source name variants within 5 km, including Praia/Playa/Beach prefixes and accents. Excluded 33 additional Surf-Forecast entries that duplicate named OSM spots. Close but distinct breaks are retained.
- Fixed nine Saint Martin entries grouped under the source's historic Netherlands Antilles label, preserving French and Dutch territory codes and their timezones.

## Independent geographic sample

341 locations checked against Geoapify: 214 stratified Surf-Forecast records (every source country group represented) and all 127 OSM additions. Evidence is in `web/data/spots/sources/americas-independent-audit-2026-09-09.json`.

Timezone agreement: 339/341. Boca Del Salado is in Baja California Sur; retained geo-tz America/Mazatlan rather than Geoapify America/Mexico_City. Espigon's Salta/Cordoba distinction has the same current UTC offset. Country differences were missing offshore results or sovereign-state versus territory codes; this sample did not uncover another misplaced country.

Geoapify returned nearby-water evidence for 124/341. The other 217 lacked indexed nearby-water evidence, including recognizable beaches. This does not establish they are inland, but it also does not independently verify their coastline position. Acceptance relies on the reviewed curated-source/OSM evidence. The sample is not an individual field or access verification of all 3,154 additions.

Country metadata for most Surf-Forecast rows comes from its directory. Timezones come from geo-tz 8.1.8 (timezone-boundary-builder), with Geoapify fallback for unresolved cases. Cache entries name the actual provider. No claim is made that Geoapify independently verified every imported point.

## Implementation review

Single-agent sequential review, per repository instructions; no independent reviewer agents. Checked source permission propagation, coordinate validation, fail-closed fetch behavior, stable existing IDs, duplicate exclusions, timezone provenance, generated catalog and nearby selection. Simplified acquisition to the public directory plus map endpoint instead of a per-spot page crawler. No runtime request or dependency on Surf-Forecast was added.

Pinned snapshots and caches make normal catalog regeneration offline. The compressed runtime catalog grows from approximately 80 KB to 191 KB. A local 100-query search sample averaged 2.22 ms; this is a local measurement, not a device-wide performance guarantee.

## Verification

- New OSM preparation script reproduces the pinned snapshot byte-for-byte.
- Import/validation checks: zero failures from new sources; all 5,060 validation lookups cached.
- Catalog rights and nearby index checks pass: 4,948 spots / 145 defaults.
- Complete unit suite: 63 files / 589 tests passed.
- Production build passed; existing large-chunk warnings remain.
- Two focused browser tests passed: existing Edam selection and new Huntington Beach / Punta de Lobos selections with exact forecast coordinates. The first Americas run exposed the test mock's hardcoded Amsterdam timezone; the mock now returns the requested timezone, matching the API contract. No production forecast change was needed.

## References

- [Surf-Forecast country directory](https://www.surf-forecast.com/countries)
- [Public map metadata](https://www.surf-forecast.com/osm/points_of_interest.json?bbox=-170,-56,-30,75&types=locations&zoom=6)
- [Tereza source entry](https://www.surf-forecast.com/breaks/Tereza)
- [USGS Kapoho](https://www.usgs.gov/node/278666)
- [Hawaii County 2018 eruption](https://recovery.hawaiicounty.gov/resources/2018-eruption)
- [Official Pohoiki information](https://www.gohawaii.jp/islands/hawaii-big-island/things-to-do/beaches/pohoiki-beach)
- [OpenStreetMap attribution](https://www.openstreetmap.org/copyright)
- [geo-tz](https://github.com/evansiroky/node-geo-tz/)
