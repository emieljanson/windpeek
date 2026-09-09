import { SPOTS } from '../spots.js'
import nearbySpots from './nearby.generated.json'

const MIN_LATITUDE = -90
const MAX_LATITUDE = 90
const MIN_LONGITUDE = -180
const MAX_LONGITUDE = 180

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
  } = {},
) {
  if (!Array.isArray(recommendations) || !Array.isArray(spots)) return null
  const recommendedIds = new Set(recommendations.map(({ id }) => id))
  return findNearestSpot(coordinates, spots.filter(spot => recommendedIds.has(spot.id)))
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
