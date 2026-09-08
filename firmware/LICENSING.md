# Windpeek firmware licensing

The combined Windpeek firmware source and binaries that include the UC8179
E1001 driver are distributed under the **GNU General Public License, version
3.0 only (GPL-3.0-only)**.

The canonical license text is available from the GNU project:
<https://www.gnu.org/licenses/gpl-3.0.txt>.

## Existing MIT code

Parts originating from `aitjcize/esp32-photoframe` remain available under the
MIT terms recorded in `LICENSE`. The MIT license is compatible with their use
inside the GPL-covered combined firmware; their original copyright and license
notices must remain intact.

## Seeed UC8179 port

The E1001 UC8179 lifecycle and waveform tables are derived from
`Seeed-Projects/Seeed_GxEPD2` under GPL v3.0. Exact provenance, the pinned
commit, source hash and local deviations are recorded in `UPSTREAM.md`.

## Separately licensed assets

Generated Berkeley Mono glyph assets under `main/fonts/` are not relicensed by
this statement. Their separate font license controls whether firmware binaries
containing them may be redistributed. Check those rights before publishing a
binary; see `../docs/fonts.md`.
