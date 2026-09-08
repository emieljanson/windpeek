import { createHash } from 'node:crypto'

import {
  compareCandidates,
  normalizeCandidate,
  parseGoogleCoordinates,
} from './candidate-normalization.mjs'

const LOCATION_HOSTS = new Set(['maps.app.goo.gl', 'www.google.com'])
const MAX_LOCATION_REDIRECTS = 5

function compactHash(value) {
  return createHash('sha256').update(String(value)).digest('hex').slice(0, 16)
}

function sourceId(record) {
  const windguruId = String(record?.windguruUrl ?? '').match(/windguru\.(?:cz|com)\/(?:int\/index\.php\?sc=)?(\d+)/i)?.[1]
  return windguruId ? `windguru-${windguruId}` : `record-${compactHash(`${record?.name}\n${record?.country}\n${record?.locationUrl}`)}`
}

async function resolveCoordinates(locationUrl, { fetchImpl, resolutions }) {
  const direct = parseGoogleCoordinates(locationUrl)
  if (direct) return direct
  if (resolutions[locationUrl]) return resolutions[locationUrl]
  if (typeof fetchImpl !== 'function') return null
  let current = locationUrl
  for (let redirect = 0; redirect <= MAX_LOCATION_REDIRECTS; redirect += 1) {
    const url = new URL(current)
    if (url.protocol !== 'https:' || url.username || url.password ||
        (url.port && url.port !== '443') || !LOCATION_HOSTS.has(url.hostname)) {
      throw new Error('unsupported-location-url')
    }
    const response = await fetchImpl(url, {
      redirect: 'manual',
      headers: { 'User-Agent': 'Windpeek spot importer' },
    })
    if (response?.status >= 300 && response.status < 400) {
      const destination = response.headers?.get?.('location')
      if (!destination || redirect === MAX_LOCATION_REDIRECTS) return null
      current = new URL(destination, url).href
      continue
    }
    if (!response?.ok) return null
    const resolved = parseGoogleCoordinates(response.url || url.href)
    if (resolved) resolutions[locationUrl] = resolved
    return resolved
  }
  return null
}

export async function importVarunRecords(records, {
  fetchImpl = globalThis.fetch,
  resolutions = {},
  releaseEligible = false,
} = {}) {
  const candidates = []
  const failures = []
  for (const [index, record] of (Array.isArray(records) ? records : []).entries()) {
    const id = sourceId(record)
    if (!String(record?.name ?? '').trim()) {
      failures.push({ sourceId: id, index, reason: 'missing-name' })
      continue
    }
    let coordinates
    try {
      coordinates = await resolveCoordinates(String(record.locationUrl ?? ''), { fetchImpl, resolutions })
    } catch {
      failures.push({ sourceId: id, index, reason: 'location-resolution-failed' })
      continue
    }
    if (!coordinates) {
      failures.push({ sourceId: id, index, reason: 'unresolved-location' })
      continue
    }
    try {
      candidates.push(normalizeCandidate({
        source: 'varun',
        sourceId: id,
        name: record.name,
        country: record.country,
        ...coordinates,
        activities: ['kitesurfing'],
        featureType: 'spot-collection',
        sourceRef: record.windguruUrl || record.locationUrl,
        releaseEligible,
      }))
    } catch (error) {
      failures.push({ sourceId: id, index, reason: error.message })
    }
  }
  return {
    candidates: candidates.sort(compareCandidates),
    failures: failures.sort((left, right) => left.sourceId.localeCompare(right.sourceId)),
    resolutions,
  }
}
