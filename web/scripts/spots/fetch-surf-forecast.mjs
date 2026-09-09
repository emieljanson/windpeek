#!/usr/bin/env node
// Fetch only spot metadata from the public country directory and map.
import { writeFile, rename } from 'node:fs/promises'
import { parseSurfForecastLocation } from './lib/surf-forecast-source.mjs'

const output = process.argv[2]
if (!output) throw new Error('Usage: node scripts/spots/fetch-surf-forecast.mjs <snapshot.json>')
const origin = 'https://www.surf-forecast.com'
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
async function fetchPage(url) {
  for (let attempt = 0; attempt < 3; attempt += 1) {
    await sleep(350)
    const response = await fetch(url, {
      headers: { 'User-Agent': 'Windscout spot catalog (permission confirmed by project owner)' },
      signal: AbortSignal.timeout(30000),
    })
    if (response.ok) return response.text()
    if (![429, 500, 502, 503, 504].includes(response.status)) throw new Error(`HTTP ${response.status}: ${url}`)
    await sleep(Math.max(3000 * (attempt + 1), Number(response.headers.get('retry-after') || 0) * 1000))
  }
  throw new Error(`Retries exhausted: ${url}`)
}

const index = await fetchPage(`${origin}/countries`)
const countries = new Map([
  ['/countries/United-States/breaks', 'United States'],
  ['/countries/Australia/breaks', 'Australia'],
])
for (const match of index.matchAll(/href="(\/countries\/[^"/]+\/breaks)"[^>]*>([^<]+)<\/a>/g)) {
  countries.set(match[1], match[2].replace(/&#39;/g, "'").replace(/&amp;/g, '&'))
}
if (countries.size < 140) throw new Error(`Country discovery incomplete: ${countries.size}`)
const spots = new Map()
for (const [url, country] of countries) {
  const html = await fetchPage(origin + url)
  const links = [...html.matchAll(/href="\/breaks\/([^"/]+)\/forecasts\/latest\/six_day"/g)]
  if (!links.length) throw new Error(`No spots discovered: ${country}`)
  for (const [, sourceId] of links) {
    if (spots.has(sourceId) && spots.get(sourceId) !== country) throw new Error(`Country conflict: ${sourceId}`)
    spots.set(sourceId, country)
  }
  console.log(`${country}: ${new Set(links.map((m) => m[1])).size}`)
}
// Match every directory entry to the precise public map metadata.
const mapUrl = `${origin}/osm/points_of_interest.json?bbox=-180,-90,180,90&types=locations&zoom=6`
const map = JSON.parse(await fetchPage(mapUrl))
if (!Array.isArray(map.locations)) throw new Error('missing-map-locations')
const locations = new Map(map.locations.map((location) => [location.filename, location]))
const records = []
for (const [sourceId, country] of spots) {
  records.push(parseSurfForecastLocation(locations.get(sourceId), sourceId, country))
}
const snapshot = {
  version: 1, fetchedAt: new Date().toISOString(),
  scope: 'Worldwide country directory, including US and Australian province groups',
  mapUrl, countries: Object.fromEntries(countries), discovered: spots.size,
  records: records.sort((a, b) => a.sourceId.localeCompare(b.sourceId)), failures: [],
}
await writeFile(`${output}.tmp`, `${JSON.stringify(snapshot, null, 2)}\n`)
await rename(`${output}.tmp`, output)
console.log(JSON.stringify({ discovered: spots.size, fetched: records.length, failures: 0 }))
