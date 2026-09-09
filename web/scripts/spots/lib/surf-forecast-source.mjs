import { compareCandidates, normalizeCandidate } from './candidate-normalization.mjs'

export function parseSurfForecastLocation(location, sourceId, country) {
  if (location?.filename !== sourceId) throw new Error('spot-identity-mismatch')
  if (!Number.isFinite(location.lat) || !Number.isFinite(location.lng) ||
      Math.abs(location.lat) > 90 || Math.abs(location.lng) > 180) throw new Error('invalid-coordinates')
  return {
    sourceId, name: location.name, country,
    latitude: location.lat, longitude: location.lng,
    breakType: location.type, lesserKnown: location.hidden === true,
  }
}

export function importSurfForecastRecords(records, { releaseEligible = false, excludedRecords = [] } = {}) {
  const candidates = [], exclusions = [], failures = []
  const excluded = new Map(excludedRecords.map((record) => [record.sourceId, record]))
  for (const record of records ?? []) {
    const sourceId = record.sourceId
    if (excluded.has(sourceId)) {
      exclusions.push(excluded.get(sourceId))
      continue
    }
    try {
      if (!Number.isFinite(record.latitude) || !Number.isFinite(record.longitude)) throw new Error('invalid-coordinates')
      candidates.push(normalizeCandidate({
        source: 'surf-forecast', sourceId, name: record.name, country: record.country,
        latitude: record.latitude, longitude: record.longitude,
        activities: ['surfing'], featureType: 'surf-break',
        sourceRef: `https://www.surf-forecast.com/breaks/${encodeURIComponent(sourceId)}`,
        releaseEligible,
      }))
    } catch (error) {
      failures.push({ sourceId, reason: error.message })
    }
  }
  return { candidates: candidates.sort(compareCandidates), exclusions, failures }
}
