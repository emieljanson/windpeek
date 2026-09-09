Release scope: no UI, styling, footer links or disclaimer changes. Source attribution remains in the data manifest and snapshots.

## Final progressive distance review — 9 September 2026

Expanded candidate review through 2, 5, 10, 15 and 25 km. Scanned 33,514 centre pairs geometrically and assessed all 73 name/alias candidates. Added 14 reviewed merges; catalog is now **8,366 locations**, with all **222 IP defaults** preserved. Historical counts below describe earlier passes.

| Distance ring | Name candidates | Additional merges |
| --- | ---: | ---: |
| 0–2 km | 18 | 3 |
| 2–5 km | 20 | 4 |
| 5–10 km | 15 | 5 |
| 10–15 km | 13 | 1 |
| 15–25 km | 7 | 1 |

The last confirmed duplicate was a misplaced Pelzerhaken point at 15.537 km. Remaining outer candidates are generic labels, repeated beach names or separate island/coastal sectors. A rebuilt scan introduces no new name candidates. Stop at 25 km: additional radius expansion is low yield for this practical cleanup. This does **not** establish measured 95% recall; different-name duplicates and ambiguous broad place names may remain.

Validation: all 64 test files / 613 tests passed, production build passed, regenerated catalog and nearby index checks passed, source rights complete.

Merges preserve old IDs and searchable aliases. Automatic grouping remains 1 km. Wider manual exceptions require recorded evidence and an explicit distance cap (maximum 25 km); ordinary reviewed merges retain their 5 km cap. Corrected Damp to the DLRG lifeguard station coordinates before grouping, since both imported pins were misplaced.

Reproducible decisions, evidence and before/after counts: `web/data/spots/sources/distance-ring-review-2026-09-09.json`. Rules and coordinate correction: `web/data/spots/location-merges.json`. The remaining 59 name candidates are explicitly classified as distinct or unconfirmed; they are not silently declared duplicate-free.

# Worldwide Surf-Forecast import and duplicate review

9 September 2026. Extends the authorized Americas import to the complete public country directory. Local work only; not deployed.

## Wider duplicate-review completion

Two follow-up passes merged another **42 named destination/facility variants**, reducing the runtime catalog from 8,422 to **8,380 locations**. All **222 IP defaults** remain. Decisions are reproducible in `web/data/spots/location-merges.json`, with source and retained IDs/names. The builder fails on missing references or reviewed centre distances beyond 5 km. Reviewed wider merges preserve existing aliases and old IDs; the 1 km bound applies to automatic grouping, not these explicit exceptions.

Queries covered all runtime centre pairs within 2 km, equivalent normalized primary/alias names within 5 km, and shared significant name tokens within 3 km. Initial results: 1,771 close pairs, two equivalent-name pairs and 209 token-overlap candidates. Token overlap often merely identifies different parts of the same island or town, so it is not automatically treated as duplication.

The remaining exact-name-style matches are retained with source evidence: [Caravelle](https://www.surf-forecast.com/breaks/Caravelle) versus [La Caravelle](https://www.surf-forecast.com/breaks/La-Caravelle) are described as beach/point versus reef breaks; [Dump Beach](https://www.surf-forecast.com/breaks/Dump-Beach) versus [The Dump](https://www.surf-forecast.com/breaks/The-Dump_1) have different locations and break descriptions. They are over 3 km apart. Other retained candidates include explicitly different bays, north/south or left/right breaks, river versus open-sea locations and different named beaches on one island. No global zero-duplicate guarantee is implied by a name/distance audit.

A discovered legacy issue was inconsistent Europe/Brussels versus Europe/Amsterdam metadata for nearby Dutch source entries. Explicit reviewed merges resolve the affected Brouwersdam, Egmond and Wijk variants to the chosen centre without globally treating different timezone labels as equivalent.

## Final 1 km forecast-location grouping

User authorized grouping neighbouring spots as one forecast location, including differently named breaks. The final runtime pass consolidates **9,801 records into 8,422 forecast locations**, retaining **1,379 former IDs and names as aliases**. All **222 curated IP defaults** remain available.

The winner order is protected built-in spots, curated priority, then shorter name and stable ID as deterministic fallbacks. Popularity is not inferred for uncurated spots. Every removed point is at most 1,000 m from its retained centre (observed maximum 999.545 m). Connected-component chaining is not used. Known country and timezone boundaries remain separate even within 1 km.

Search matches alias names and returns the retained location. `getSpot` resolves former IDs to that location, preserving saved-ID lookup. Coordinates and forecasts use the retained centre. Personal spots are not part of the generated consolidation. Source snapshots retain the original coordinates.

Verification: complete pre-new-test suite passed (605 tests), and the new consolidation tests plus final catalog/nearby tests passed (55 focused tests). Checks cover aliases, old Third Avenue ID resolution, winner preference, input immutability, boundary separation and no transitive merging. All 1,379 alias distances were measured against the pre-consolidation catalog. Build and focused browser results recorded in the task.

The earlier sections below retain historical counts.

## Aggressive deduplication follow-up

User explicitly authorized more aggressive consolidation. Final catalog now **9,801 spots**, removing another **68 overlapping records**. All **222 IP defaults** remain valid. Automatic suppressions increase from 107 to 175; explicit source exclusions remain separately recorded.

Additional rules: normalize Praia/Playa/Plage/Beach and connecting words for same-place matches within 3 km; retain point-versus-beach distinctions; consolidate non-surf facilities within 75 m as one forecast location. Differently named surf breaks remain separate. Direct-winner matching still prevents chain merging beyond the thresholds.

Remaining review output: three wider/structurally distinct name-variant pairs and 28 proximity-only pairs. This is not proof of zero duplicates worldwide. The approach deliberately tolerates separate named breaks rather than treating every nearby wave as the same place.

Regression checks cover beach aliases, adjacent facilities, separate Pipeline/Backdoor breaks, distant namesakes and point-versus-beach distinctions, plus all curated regional defaults. Earlier sections below retain their historical counts.

## Final location and IP-default pass

Final catalog: **9,869 locations**. The IP shortlist grows from **145 to 222** (+77), covering Australia, New Zealand, Southeast/East/South Asia, Africa, Europe and additional American coastal cities. Exact selected IDs and names are pinned in `popular-spots.json`; no arbitrary density-based auto-promotion was added.

The additional full country/timezone grouping review found three problematic points: Cement Factory has a positive latitude inconsistent with Indonesia; Eilat Green Beach's offshore pin produces a Jordan timezone; Ocos is west of the named Guatemalan destination on the Mexican side. All three are excluded pending reliable corrected coordinates. No coordinates were guessed. Gaza, Vama Veche, Northern Ireland, Norfolk Island and Crown Dependency country codes now reflect reviewed geography rather than broad directory groups. Provider provenance is retained in the cache.

Two more same-beach aliases (Playa Jaco and Praia Cardoso) are excluded in favor of Jaco Beach and Cardoso. Other name-variant/proximity pairs are retained conservatively. In particular, beach versus point/reef breaks and separate Steamer Lane breaks are not merged. Remaining uncertain pairs and legacy club/school records are not automatically promoted into the IP shortlist. This does not claim every legacy catalog facility is removed or every source point has been visited.

All 77 new IP defaults received an independent Geoapify comparison, saved in `web/data/spots/sources/ip-default-audit-2026-09-09.json`. Watamu's coordinate is on the Kenyan coast; Geoapify returned Spain, the same inconsistent response observed for nearby Malindi, so the independently plausible source coordinate and Africa/Nairobi timezone are retained. Missing offshore country/water matches are not treated as proof of a bad spot.

Examples exercised: Sydney → Bondi Beach, Perth → Cottesloe Beach, Melbourne → Torquay Front Beach, Auckland → Piha North, Wellington → Lyall Bay, Bali → Kuta Beach, Phuket → Kata Beach, Durban → Dairy Beach, Tokyo → Shonan, Miami → South Beach, San Diego → La Jolla Shores. Broad inland IP estimates still select the nearest curated spot; existing distance behavior is unchanged. A manually chosen spot remains under the user's control.

Final verification: 603 unit tests passed, production build passed, catalog rights/index checks passed. Browser checks cover IP-derived local forecasts and the existing timeout/user-input behavior. No deployment or commit was performed.

The sections below record the preceding worldwide import and its initial counts.

## Coverage and result

- 8,032 unique Surf-Forecast records across 146 country/territory groups, all matched to the public worldwide map. US and Australia are explicitly included because the index presents them through province groups.
- Replaces the Americas source in the manifest; it does not import that subset twice. Prior permission and all prior manual exclusions are preserved.
- 48 explicit Surf-Forecast exclusions: 43 reviewed duplicate names and five problematic locations (the prior four plus Assinie).
- Combined candidates: 9,980. Automatic duplicate suppression: 107. One existing renderer-name rejection. Runtime catalog: 9,874 locations, up from 4,948 (+4,926). All 4,948 previous runtime IDs remain available.
- Nearby defaults remain the separately curated set of 145; the full worldwide catalog is searchable.

## Duplicate behavior

Previously, any points within 75 m could be transitively merged, even when names identified different breaks. Now automatic suppression requires equivalent names within 5 km. Case, accents and punctuation are normalized; directional terms are preserved. Each removed record must match a retained winner directly. A chain of nearby points cannot merge distant endpoints. Winner ordering remains deterministic and honors existing source priorities.

This intentionally restores some previously proximity-suppressed records, including legacy OSM facilities. It does not clean every legacy school/club out of the original global OSM source. The reviewed Americas OSM filtering remains in place.

Name variants and proximity-only pairs are exported for review, not automatically removed. Ten additional reviewed variants were excluded:

| Removed Surf-Forecast ID | Retained source |
|---|---|
| Blyth-and-Seaton | varun:windguru-500685 |
| Hendaye-Plage | varun:windguru-48576 |
| Praia-do-Bordeira | varun:windguru-112689 |
| Praiado-Guincho | varun:windguru-1102295 |
| Banzai_2 | varun:windguru-159431 |
| Praiada-Barra | varun:windguru-48948 |
| Six-Fours | varun:windguru-824 |
| Playa-Langosta | surf-forecast:Langosta |
| Neurim-Youth-Beach | surf-forecast:Neurim-Beach |
| Northwall | surf-forecast:Ballina-North-Wall |

The final report retains 17 name-variant pairs and 100 proximity-only pairs. Some overlap. They are ambiguous or distinct locations, not 117 proven duplicates. Examples deliberately retained include separate Steamer Lane breaks and a general beach alongside a named break. Review evidence is names, source identities and distances; these pairs have not all been independently field-verified.

Report: `web/data/spots/sources/worldwide-duplicate-review-2026-09-09.json`. Refresh with `node scripts/spots/review-duplicates.mjs <report.json>` from `web/`.

## Geographic gut check

187 stratified Surf-Forecast samples, with every source country group represented, compared against an independent Geoapify cache (280 new requests, 94 endpoint cache hits). Saved audit retains both initial and corrected metadata.

- Found and corrected historical country aliases produced by Intl: France FX → FR, United Kingdom UK → GB, Russia SU → RU, Benin DY → BJ. Canonicalization is in the acquisition script and regression checks cover the generated catalog.
- Hong Kong and Macau retain territory codes using their geographic timezone boundaries, despite the source grouping them under China.
- Beyin Beach is grouped under Ivory Coast by the directory but lies in Ghana; its reviewed country code is GH.
- Assinie_1 has a Ghana coordinate inconsistent with the named Ivory Coast destination. Excluded rather than inventing a corrected pin. [Source coordinate](https://www.surf-forecast.com/breaks/Assinie_1), [independent Assinie surf location](https://www.wannasurf.com/spot/Africa/Ivory_Coast/assinie/).
- Kept the reviewed Dutch territory for Cupecoy even though Geoapify returns the French side; it is one of the prior explicit Saint Martin mappings.
- Geoapify returned Spain/Europe-Madrid for Malindi Bay at -3.198, 40.126. Retained Kenya/Africa-Nairobi from geographic evidence; the reverse-geocoder result is inconsistent with the coordinate. [Malindi Bay map](https://mapcarta.com/N318591728).

Final timezone agreement is 185/187 (Cupecoy and Malindi differences above). Nearby-water evidence was positive for 57/187; absent for 130. The water POI lookup is incomplete offshore, so these results do not independently confirm or disprove every surf location. The catalog relies on source metadata and timezone boundaries plus this sample and explicit review; this is not a guarantee that all worldwide entries are current, accessible or individually verified.

Audit: `web/data/spots/sources/worldwide-geographic-audit-2026-09-09.json`.

## Review and verification

Sequential main-agent review per repository instructions. Reviewed acquisition completeness, country alias collisions, source permission propagation, deterministic duplicate selection, non-transitive suppression, generated data and old-ID preservation. No independent review agents were used.

- Full unit suite: 63 files, 591 tests passed before the final country-code regression test; focused final tests include that additional test.
- Browser checks passed for existing Edam selection and Huntington Beach, Punta de Lobos and Uluwatu, including exact forecast request coordinates and matching timezones.
- Production build passed with existing large-chunk warnings. The worldwide catalog increases the configurator bundle; build output was approximately 381 KB gzip for that chunk before the final metadata correction.
- Catalog source-rights and nearby checks passed at 9,874 spots / 145 defaults.
- Existing legacy OSM import failures (1,525) are unchanged; worldwide Surf-Forecast import has zero extraction failures.
- Source data is pinned. The browser makes no Surf-Forecast requests. Normal regeneration uses the committed validation cache.
