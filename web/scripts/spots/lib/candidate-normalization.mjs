const ACTIVITY_ALIASES = new Map([
  ['kitesurf', 'kitesurfing'],
  ['kitesurfing', 'kitesurfing'],
  ['kite_surfing', 'kitesurfing'],
  ['kiteboarding', 'kitesurfing'],
  ['surfing', 'surfing'],
  ['sailing', 'sailing'],
  ['windsurf', 'windsurfing'],
  ['windsurfing', 'windsurfing'],
  ['wingfoil', 'wingfoil'],
  ['wingfoiling', 'wingfoil'],
  ['wing_foil', 'wingfoil'],
  ['wing_foiling', 'wingfoil'],
])

function cleanText(value) {
  return String(value ?? '').normalize('NFC').trim().replace(/\s+/g, ' ')
}

export function normalizeActivities(value) {
  const values = Array.isArray(value) ? value : String(value ?? '').split(';')
  return [...new Set(values
    .map((activity) => ACTIVITY_ALIASES.get(cleanText(activity).toLowerCase()))
    .filter(Boolean))]
    .sort((left, right) => left.localeCompare(right))
}

function validCoordinates(latitude, longitude) {
  return Number.isFinite(latitude) && latitude >= -90 && latitude <= 90 &&
    Number.isFinite(longitude) && longitude >= -180 && longitude <= 180
}

function coordinatePair(value) {
  const match = String(value ?? '').match(/(-?\d{1,2}(?:\.\d+)?)\s*,\s*(-?\d{1,3}(?:\.\d+)?)/)
  if (!match) return null
  const latitude = Number(match[1])
  const longitude = Number(match[2])
  return validCoordinates(latitude, longitude) ? { latitude, longitude } : null
}

export function parseGoogleCoordinates(value) {
  const raw = String(value ?? '').trim()
  if (!raw) return null
  try {
    const url = new URL(raw)
    for (const key of ['q', 'query', 'll']) {
      const parsed = coordinatePair(url.searchParams.get(key))
      if (parsed) return parsed
    }
    const atCoordinates = decodeURIComponent(url.pathname).match(/@(-?\d{1,2}(?:\.\d+)?),(-?\d{1,3}(?:\.\d+)?)/)
    if (atCoordinates) return coordinatePair(`${atCoordinates[1]},${atCoordinates[2]}`)
  } catch {
    return coordinatePair(raw)
  }
  return coordinatePair(decodeURIComponent(raw))
}

export function normalizeCandidate(candidate) {
  const source = cleanText(candidate?.source).toLowerCase()
  const sourceId = cleanText(candidate?.sourceId)
  const name = cleanText(candidate?.name)
  const latitude = Number(candidate?.latitude)
  const longitude = Number(candidate?.longitude)
  const activities = normalizeActivities(candidate?.activities)
  if (!source) throw new Error('missing-source')
  if (!sourceId) throw new Error('missing-source-id')
  if (!name) throw new Error('missing-name')
  if (!validCoordinates(latitude, longitude)) throw new Error('invalid-coordinates')
  if (!activities.length) throw new Error('unsupported-activity')
  return Object.freeze({
    id: `${source}:${sourceId}`,
    source,
    sourceId,
    name,
    country: cleanText(candidate?.country),
    latitude: Number(latitude.toFixed(6)),
    longitude: Number(longitude.toFixed(6)),
    activities: Object.freeze(activities),
    featureType: cleanText(candidate?.featureType),
    sourceRef: cleanText(candidate?.sourceRef),
    releaseEligible: candidate?.releaseEligible === true,
    flags: Object.freeze([...new Set(candidate?.flags ?? [])].sort()),
  })
}

export function compareCandidates(left, right) {
  return left.id.localeCompare(right.id)
}
