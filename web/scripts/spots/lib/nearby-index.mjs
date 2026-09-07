import { stableSpotId } from '../../../src/spots/spotIdentity.js'

const PLACE_FEATURE_TYPES = new Set(['beach', 'spot-collection', 'watersport-location'])
const ORGANIZATION_WORDS = /\b(?:academy|camp(?:ing)?|center|centre|centrum|club|rental|rentals|school|schule|surfschule|shop|vereniging)\b/i
const GENERIC_SPOT_NAMES = /^(?:kite(?:\s*surf(?:ing)?)?|windsurf(?:ing)?|wingfoil(?:ing)?|surf(?:ing)?|(?:kite|kitesurf|surf|windsurf|wingfoil)\s+(?:launch|spot)|foil kite\s*\/\s*wing launch)$/i
const LAUNCH_WORD = /\blaunch\b/i

export function buildNearbyIndex({ candidates, catalog, popularSpots, baselineSpotIds = [] }) {
  const catalogById = new Map(catalog.map((spot) => [spot.id, spot]))
  const recommended = new Map()
  const popularIds = new Set()

  for (const id of baselineSpotIds) {
    const spot = catalogById.get(id)
    if (!spot || !isPlaceName(spot.name)) {
      throw new Error(`Baseline spot ${id} must be a named place in the catalog.`)
    }
    recommended.set(id, 1)
  }

  for (const candidate of candidates) {
    if (!isSurfSpot(candidate)) continue
    const id = stableSpotId(candidate.id)
    if (catalogById.has(id)) recommended.set(id, 1)
  }

  for (const popularSpot of popularSpots) {
    if (popularIds.has(popularSpot.id)) {
      throw new Error(`Popular spot ${popularSpot.id} is listed more than once.`)
    }
    popularIds.add(popularSpot.id)
    const catalogSpot = catalogById.get(popularSpot.id)
    if (!catalogSpot) throw new Error(`Popular spot ${popularSpot.id} is missing from the catalog.`)
    if (!isPlaceName(catalogSpot.name)) {
      throw new Error(`Popular spot ${popularSpot.id} is not a geographic place name.`)
    }
    if (catalogSpot.name !== popularSpot.name) {
      throw new Error(`Popular spot ${popularSpot.id} changed name from ${popularSpot.name} to ${catalogSpot.name}.`)
    }
    if (!Number.isInteger(popularSpot.priority) || popularSpot.priority < 2) {
      throw new Error(`Popular spot ${popularSpot.id} needs an integer priority of 2 or higher.`)
    }
    recommended.set(popularSpot.id, popularSpot.priority)
  }

  return [...recommended]
    .map(([id, priority]) => ({ id, priority }))
    .sort((left, right) => left.id.localeCompare(right.id))
}

function isSurfSpot(candidate) {
  const activities = new Set(candidate.activities ?? [])
  return PLACE_FEATURE_TYPES.has(candidate.featureType)
    && isPlaceName(candidate.name)
    && ['kitesurfing', 'windsurfing', 'wingfoil'].some((activity) => activities.has(activity))
}

function isPlaceName(name) {
  const normalizedName = String(name ?? '').trim()
  return normalizedName.length > 0
    && !ORGANIZATION_WORDS.test(normalizedName)
    && !GENERIC_SPOT_NAMES.test(normalizedName)
    && !LAUNCH_WORD.test(normalizedName)
}
