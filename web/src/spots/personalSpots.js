import { RENDERER_TEXT_CAPACITIES, textFitsRenderer } from '../renderer/contract'
import { availableStorage } from '../storage'
import { validTimezone } from '../timezone'

export const PERSONAL_SPOTS_STORAGE_KEY = 'windpeek.personal-spots'
const PERSONAL_SPOTS_VERSION = 1

function slug(value) {
  return value.normalize('NFKD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-|-$/g, '')
    .slice(0, 32) || 'spot'
}

function coordinateId(value, offset) {
  return Math.round((value + offset) * 100000).toString(36)
}

function validPersonalSpot(spot) {
  return Boolean(
    spot && spot.personal === true &&
    typeof spot.id === 'string' && spot.id.startsWith('personal-') &&
    typeof spot.name === 'string' && spot.name.trim() &&
    typeof spot.displayName === 'string' &&
    textFitsRenderer(spot.displayName, RENDERER_TEXT_CAPACITIES.spotName) &&
    Number.isFinite(spot.latitude) && spot.latitude >= -90 && spot.latitude <= 90 &&
    Number.isFinite(spot.longitude) && spot.longitude >= -180 && spot.longitude <= 180 &&
    typeof spot.timezone === 'string' && validTimezone(spot.timezone) &&
    (spot.countryCode == null || spot.countryCode === '' || /^[a-z]{2}$/.test(spot.countryCode)) &&
    (spot.providerRef == null || typeof spot.providerRef === 'string')
  )
}

export function createPersonalSpot({
  name,
  latitude,
  longitude,
  timezone,
  countryCode = '',
  providerRef = '',
}) {
  const cleanName = String(name ?? '').trim().replace(/\s+/g, ' ')
  const displayName = cleanName.toLocaleUpperCase()
  if (!cleanName || !textFitsRenderer(displayName, RENDERER_TEXT_CAPACITIES.spotName)) {
    throw new Error('Enter a shorter spot name.')
  }
  const lat = Number(latitude)
  const lon = Number(longitude)
  if (!Number.isFinite(lat) || lat < -90 || lat > 90 ||
      !Number.isFinite(lon) || lon < -180 || lon > 180) {
    throw new Error('Choose valid coordinates for this spot.')
  }
  if (!validTimezone(timezone)) throw new Error('Choose a location with a valid timezone.')
  const spot = {
    id: `personal-${slug(cleanName)}-${coordinateId(lat, 90)}-${coordinateId(lon, 180)}`,
    name: cleanName,
    displayName,
    latitude: Number(lat.toFixed(6)),
    longitude: Number(lon.toFixed(6)),
    timezone,
    countryCode: String(countryCode ?? '').trim().toLowerCase(),
    providerRef: String(providerRef ?? '').slice(0, 160),
    personal: true,
  }
  if (!validPersonalSpot(spot)) throw new Error('This spot could not be saved.')
  return Object.freeze(spot)
}

export function readPersonalSpots(storage) {
  const target = availableStorage(storage)
  if (!target) return []
  try {
    const envelope = JSON.parse(target.getItem(PERSONAL_SPOTS_STORAGE_KEY))
    if (envelope?.version !== PERSONAL_SPOTS_VERSION || !Array.isArray(envelope.spots)) return []
    return envelope.spots.filter(validPersonalSpot).map((spot) => Object.freeze({ ...spot }))
  } catch {
    return []
  }
}

export function writePersonalSpot(spot, storage) {
  if (!validPersonalSpot(spot)) return false
  const target = availableStorage(storage)
  if (!target) return false
  const spots = readPersonalSpots(target)
  const index = spots.findIndex((candidate) => candidate.id === spot.id)
  if (index >= 0) spots[index] = spot
  else spots.push(spot)
  try {
    target.setItem(PERSONAL_SPOTS_STORAGE_KEY, JSON.stringify({
      version: PERSONAL_SPOTS_VERSION,
      spots,
    }))
    return true
  } catch {
    return false
  }
}
