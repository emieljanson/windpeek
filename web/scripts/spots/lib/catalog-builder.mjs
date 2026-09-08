import { textFitsRenderer, RENDERER_TEXT_CAPACITIES } from '../../../src/renderer/contract.js'
import { stableSpotId } from '../../../src/spots/spotIdentity.js'

function validTimezone(timezone) {
  try {
    new Intl.DateTimeFormat('en', { timeZone: timezone }).format()
    return typeof timezone === 'string' && Boolean(timezone)
  } catch {
    return false
  }
}

function runtimeSpot({ id, name, latitude, longitude, timezone, countryCode }) {
  const cleanName = String(name ?? '').trim().replace(/\s+/g, ' ')
  const displayName = cleanName.toLocaleUpperCase()
  const lat = Number(latitude)
  const lon = Number(longitude)
  if (!Number.isFinite(lat) || lat < -90 || lat > 90 || !Number.isFinite(lon) || lon < -180 || lon > 180) {
    throw new Error(`Spot ${id} has invalid coordinates.`)
  }
  if (!cleanName || !textFitsRenderer(displayName, RENDERER_TEXT_CAPACITIES.spotName)) {
    throw new Error(`Spot ${id} does not fit the renderer text contract.`)
  }
  if (!validTimezone(timezone)) throw new Error(`Spot ${id} has an invalid timezone.`)
  const country = String(countryCode ?? '').trim().toLowerCase()
  if (country && !/^[a-z]{2}$/.test(country)) {
    throw new Error(`Spot ${id} has an invalid country code.`)
  }
  return Object.freeze({
    id: String(id),
    name: cleanName,
    displayName,
    latitude: Number(lat.toFixed(6)),
    longitude: Number(lon.toFixed(6)),
    timezone,
    countryCode: country,
  })
}

export function buildRuntimeCatalog({ existing, candidates, validationResults, decisions }) {
  const existingSpots = existing.map(runtimeSpot)
  const existingNames = new Set(existingSpots.map((spot) => spot.name.toLocaleLowerCase('en')))
  const resultsById = new Map(validationResults.map((result) => [result.candidateId, result]))
  const decisionsById = new Map(decisions.map((decision) => [decision.candidateId, decision]))
  const generated = []
  for (const candidate of candidates) {
    if (candidate.releaseEligible !== true) continue
    const validation = resultsById.get(candidate.id)
    if (!validation) continue
    if (validation.outcome === 'accepted') {
      generated.push(runtimeSpot({
        id: stableSpotId(candidate.id),
        name: candidate.name,
        latitude: candidate.latitude,
        longitude: candidate.longitude,
        timezone: validation.timezone,
        countryCode: validation.countryCode,
      }))
      continue
    }
    const decision = decisionsById.get(candidate.id)
    if (validation.outcome !== 'needs-review' || decision?.action !== 'approve' ||
        decision.evidenceFingerprint !== validation.evidenceFingerprint) continue
    generated.push(runtimeSpot({
      id: decision.windpeekId || stableSpotId(candidate.id),
      name: decision.name,
      latitude: decision.latitude,
      longitude: decision.longitude,
      timezone: decision.timezone || validation.timezone,
      countryCode: validation.countryCode,
    }))
  }
  const catalog = [
    ...existingSpots,
    ...generated
      .filter((spot) => !existingNames.has(spot.name.toLocaleLowerCase('en')))
      .sort((left, right) => left.name.localeCompare(right.name) || left.id.localeCompare(right.id)),
  ]
  const ids = new Set()
  for (const spot of catalog) {
    if (ids.has(spot.id)) throw new Error(`Duplicate spot id: ${spot.id}`)
    ids.add(spot.id)
  }
  return catalog
}
