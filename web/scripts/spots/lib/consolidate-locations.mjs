import { distanceMeters } from './duplicate-detection.mjs'

// Automatic grouping uses direct centres within 1 km, not connected components.
// Country/timezone boundaries remain separate unless explicitly reviewed below.
export function consolidateLocations(catalog, { preferred = [], protectedIds = [], reviewedMerges = [], coordinateCorrections = [] } = {}) {
  catalog = catalog.map((spot) => ({ ...spot }))
  for (const correction of coordinateCorrections) {
    const spot = catalog.find((s) => s.id === correction.id)
    if (!spot) throw new Error(`Stale coordinate correction: ${correction.id}`)
    if (typeof correction.evidence !== 'string' || !correction.evidence.trim() || !Number.isFinite(correction.latitude) || Math.abs(correction.latitude) > 90 ||
      !Number.isFinite(correction.longitude) || Math.abs(correction.longitude) > 180) throw new Error('Invalid reviewed coordinate correction')
    spot.latitude = correction.latitude
    spot.longitude = correction.longitude
  }
  const priorities = new Map(preferred.map((spot) => [spot.id, spot.priority]))
  const positions = new Map(catalog.map((spot, index) => [spot.id, index]))
  const protectedSet = new Set(protectedIds)
  const ordered = [...catalog].sort((a, b) =>
    Number(protectedSet.has(b.id)) - Number(protectedSet.has(a.id)) ||
    (priorities.get(b.id) ?? 0) - (priorities.get(a.id) ?? 0) ||
    a.name.length - b.name.length || a.id.localeCompare(b.id))
  const retained = []
  for (const spot of ordered) {
    const match = retained.find((other) =>
      other.timezone === spot.timezone &&
      (!other.countryCode || !spot.countryCode || other.countryCode === spot.countryCode) &&
      Math.abs(other.latitude - spot.latitude) <= 0.01 && distanceMeters(other, spot) <= 1000)
    if (!match) {
      retained.push({ ...spot })
      continue
    }
    match.aliases = [...new Set([...(match.aliases ?? []), spot.name, ...(spot.aliases ?? [])])]
      .filter((name) => name !== match.name).sort()
    match.aliasIds = [...new Set([...(match.aliasIds ?? []), spot.id, ...(spot.aliasIds ?? [])])].sort()
  }
  for (const merge of reviewedMerges) {
    const limit = merge.maxDistanceMeters ?? 5000
    if (!Number.isFinite(limit) || limit < 0 || limit > 25000 || (limit > 5000 && (typeof merge.evidence !== 'string' || !merge.evidence.trim()))) {
      throw new Error('Wider reviewed merges require evidence and a distance limit of at most 25 km')
    }
    const from = retained.find((s) => s.id === merge.fromId || s.aliasIds?.includes(merge.fromId))
    const to = retained.find((s) => s.id === merge.toId || s.aliasIds?.includes(merge.toId))
    if (!from || !to) throw new Error(`Stale reviewed location merge: ${merge.fromId} -> ${merge.toId}`)
    if (from === to) continue
    if (distanceMeters(from, to) > limit) throw new Error(`Reviewed location merge exceeds ${limit / 1000} km`)
    to.aliases = [...new Set([...(to.aliases ?? []), from.name, ...(from.aliases ?? [])])].filter((name) => name !== to.name).sort()
    to.aliasIds = [...new Set([...(to.aliasIds ?? []), from.id, ...(from.aliasIds ?? [])])].sort()
    retained.splice(retained.indexOf(from), 1)
  }
  return retained.sort((a, b) => positions.get(a.id) - positions.get(b.id))
}
