#!/usr/bin/env node
// Optional offline acquisition tool; geo-tz is not a browser/runtime dependency.
// Install geo-tz@8.1.8 in a separate tools directory and expose it via NODE_PATH.
import { createRequire } from 'node:module'
import { readFile, writeFile, rename } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { cacheKeyForCandidate } from './lib/geoapify-validation.mjs'

const require = createRequire(import.meta.url)
const { find } = require('geo-tz/all')
const geoTzVersion = require(path.resolve(path.dirname(require.resolve('geo-tz/all')), '../package.json')).version
const dataRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../data/spots')
const input = JSON.parse(await readFile(path.join(dataRoot, 'candidates.json'), 'utf8'))
const cachePath = path.join(dataRoot, 'validation-cache.json')
const cache = JSON.parse(await readFile(cachePath, 'utf8'))
const countries = new Map()
const names = new Intl.DisplayNames(['en'], { type: 'region' })
for (let a = 65; a <= 90; a += 1) for (let b = 65; b <= 90; b += 1) {
  const code = String.fromCharCode(a, b), name = names.of(code)
  // Intl also recognizes retired aliases (FX, SU, DY, UK); canonicalize them.
  if (name !== code) countries.set(name.toLowerCase(), new Intl.Locale(`und-${code}`).region.toLowerCase())
}
for (const [name, code] of Object.entries({
  'British Virgin Islands': 'vg', 'US Virgin Islands': 'vi', 'Saint Barthélemy': 'bl',
  'Saint Kitts and Nevis': 'kn', 'Saint Lucia': 'lc', 'Falkland Islands': 'fk',
  'Trinidad and Tobago': 'tt', 'Turks and Caicos Islands': 'tc',
  'Spain (Europe)': 'es', 'Spain (Africa)': 'es', 'East Timor': 'tl',
  'Cape Verde': 'cv', "Côte d'Ivoire": 'ci', 'Côte d&#39;Ivoire': 'ci',
  'Republic of the Congo': 'cg', 'Reunion': 're', 'Brunei Darussalam': 'bn', 'Turkey': 'tr',
})) countries.set(name.toLowerCase(), code)

let added = 0
const unresolved = []
function reviewedCountry(candidate, timezone) {
  if (candidate.sourceId === 'Beyin-Beach') return 'gh'
  if (candidate.sourceId === 'Vama-Veche') return 'ro'
  if (candidate.sourceId === 'Gaza-Harbourmouth') return 'ps'
  const territories = {
    'China/Asia/Hong_Kong': 'hk', 'China/Asia/Macau': 'mo',
    'Ireland/Europe/London': 'gb', 'Australia/Pacific/Norfolk': 'nf',
    'United Kingdom/Europe/Guernsey': 'gg', 'United Kingdom/Europe/Jersey': 'je',
    'United Kingdom/Europe/Isle_of_Man': 'im',
  }
  return territories[`${candidate.country}/${timezone}`]
}
const saintMartinTerritories = new Map([
  ['Cupecoy', 'sx'], ['Guana-Bay', 'sx'], ['Mullet-Bay', 'sx'],
  ['Friars-Bay', 'mf'], ['Galion', 'mf'], ['Garbage-Heap', 'mf'],
  ['Le-Gallion', 'mf'], ['The-Bowl_1', 'mf'], ['Wilderness_1', 'mf'],
])
for (const candidate of input.candidates.filter((c) => c.source === 'surf-forecast')) {
  const key = cacheKeyForCandidate(candidate)
  // The source's historic Netherlands Antilles group includes both halves of
  // Saint Martin. Preserve the territory, rather than the sovereign-state flag.
  const territory = candidate.country === 'Netherlands Antilles' ? saintMartinTerritories.get(candidate.sourceId) : null
  const saintMartin = Boolean(territory)
  const reviewed = reviewedCountry(candidate, cache[key]?.reverse?.timezone)
  if (reviewed && cache[key]?.reverse) {
    cache[key].reverse.countryCode = reviewed
    cache[key].reverse.countryProvider = 'Reviewed territory/country mapping (2026-09-09)'
  }
  if (cache[key]?.reverse?.countryProvider === 'Surf-Forecast country directory') {
    const reverse = cache[key].reverse
    reverse.countryCode = countries.get(candidate.country.toLowerCase()) || reverse.countryCode
  }
  if (cache[key]?.reverse && !saintMartin) continue
  const zones = saintMartin
    ? [territory === 'sx' ? 'America/Lower_Princes' : 'America/Marigot']
    : find(candidate.latitude, candidate.longitude)
  const countryCode = territory || reviewedCountry(candidate, zones[0]) || countries.get(candidate.country.toLowerCase())
  if (!countryCode || zones.length !== 1 || zones[0].startsWith('Etc/')) {
    unresolved.push(candidate.id)
    continue
  }
  cache[key] = { ...cache[key], reverse: {
    countryCode, timezone: zones[0],
    countryProvider: saintMartin ? 'Reviewed Saint Martin territory mapping (2026-09-09)' : reviewedCountry(candidate, zones[0]) ? 'Reviewed territory/country mapping (2026-09-09)' : 'Surf-Forecast country directory',
    timezoneProvider: saintMartin ? 'Reviewed Saint Martin territory mapping (2026-09-09)' : `geo-tz@${geoTzVersion}/all (timezone-boundary-builder, ODbL-1.0)`,
  } }
  added += 1
}
await writeFile(`${cachePath}.tmp`, `${JSON.stringify(cache, null, 2)}\n`)
await rename(`${cachePath}.tmp`, cachePath)
console.log(JSON.stringify({ added, unresolved }, null, 2))
