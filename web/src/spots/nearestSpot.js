import { SPOTS } from '../spots.js'
import nearbySpots from './nearby.generated.json'

const MIN_LATITUDE = -90
const MAX_LATITUDE = 90
const MIN_LONGITUDE = -180
const MAX_LONGITUDE = 180
const EARTH_RADIUS_KM = 6371
export const NEARBY_DEFAULT_RADIUS_KM = 75
const DEFAULT_NEARBY_PRIORITIES = new Map(
  nearbySpots.map(({ id, priority }) => [id, priority]),
)

export function normalizeCoordinates(value) {
  if (!value || typeof value !== 'object') return null

  const { latitude, longitude } = value
  if (!Number.isFinite(latitude) || !Number.isFinite(longitude)) return null
  if (latitude < MIN_LATITUDE || latitude > MAX_LATITUDE) return null
  if (longitude < MIN_LONGITUDE || longitude > MAX_LONGITUDE) return null

  return { latitude, longitude }
}

export function findNearestSpot(coordinates, spots = SPOTS) {
  const origin = normalizeCoordinates(coordinates)
  if (!origin || !Array.isArray(spots)) return null

  let nearestSpot = null
  let nearestDistance = Number.POSITIVE_INFINITY

  for (const spot of spots) {
    const candidate = normalizeCoordinates(spot)
    if (!candidate) continue

    const distance = haversineDistance(origin, candidate)
    if (distance < nearestDistance) {
      nearestDistance = distance
      nearestSpot = spot
    }
  }

  return nearestSpot
}

export function findNearbyDefaultSpot(
  coordinates,
  spots = SPOTS,
  {
    recommendations = nearbySpots,
    maxDistanceKm = NEARBY_DEFAULT_RADIUS_KM,
  } = {},
) {
  const origin = normalizeCoordinates(coordinates)
  if (!origin || !Array.isArray(spots) || !Array.isArray(recommendations)) return null

  const priorityById = recommendations === nearbySpots
    ? DEFAULT_NEARBY_PRIORITIES
    : new Map(recommendations.map(({ id, priority }) => [id, priority]))
  let bestSpot = null
  let bestPriority = Number.NEGATIVE_INFINITY
  let bestDistanceKm = Number.POSITIVE_INFINITY
  let nearestSpot = null
  let nearestDistanceKm = Number.POSITIVE_INFINITY

  for (const spot of spots) {
    const priority = priorityById.get(spot.id)
    const candidate = normalizeCoordinates(spot)
    if (!Number.isFinite(priority) || !candidate) continue

    const distanceKm = haversineDistance(origin, candidate) * EARTH_RADIUS_KM
    if (distanceKm < nearestDistanceKm) {
      nearestSpot = spot
      nearestDistanceKm = distanceKm
    }
    if (distanceKm > maxDistanceKm) continue
    if (priority > bestPriority || (priority === bestPriority && distanceKm < bestDistanceKm)) {
      bestSpot = spot
      bestPriority = priority
      bestDistanceKm = distanceKm
    }
  }

  return bestSpot ?? nearestSpot
}

function haversineDistance(origin, destination) {
  const originLatitude = toRadians(origin.latitude)
  const destinationLatitude = toRadians(destination.latitude)
  const latitudeDelta = destinationLatitude - originLatitude
  const longitudeDelta = toRadians(destination.longitude - origin.longitude)

  const latitudeComponent = Math.sin(latitudeDelta / 2) ** 2
  const longitudeComponent = Math.sin(longitudeDelta / 2) ** 2
    * Math.cos(originLatitude)
    * Math.cos(destinationLatitude)

  const haversine = Math.min(1, Math.max(0, latitudeComponent + longitudeComponent))
  return 2 * Math.asin(Math.sqrt(haversine))
}

function toRadians(degrees) {
  return degrees * Math.PI / 180
}
