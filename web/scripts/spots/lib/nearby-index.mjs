import { stableSpotId } from '../../../src/spots/spotIdentity.js'

export function buildNearbyIndex({ candidates, catalog, popularSpots }) {
  const catalogById = new Map(catalog.map((spot) => [spot.id, spot]))
  const recommended = new Map()
  const popularIds = new Set()

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
  const excludedTypes = new Set(['club', 'school', 'marina'])
  return !excludedTypes.has(candidate.featureType)
    && ['kitesurfing', 'windsurfing', 'wingfoil'].some((activity) => activities.has(activity))
}
