import { normalizeCoordinates } from '../spots/nearestSpot.js'

export const IP_LOCATION_TIMEOUT_MS = 3_000

export async function fetchIpLocation({
  endpoint = import.meta.env.VITE_NEARBY_LOCATION_URL,
  fetcher = globalThis.fetch,
  timeoutMs = IP_LOCATION_TIMEOUT_MS,
} = {}) {
  const locationEndpoint = typeof endpoint === 'string' ? endpoint.trim() : ''
  if (!locationEndpoint || typeof fetcher !== 'function') return null

  const controller = new AbortController()
  const timeoutId = setTimeout(() => controller.abort(), timeoutMs)

  try {
    const response = await fetcher(locationEndpoint, {
      method: 'GET',
      cache: 'no-store',
      credentials: 'omit',
      signal: controller.signal,
    })
    if (!response.ok) return null

    const data = await response.json()
    const coordinates = normalizeCoordinates(data)
    if (!coordinates) return null
    const countryCode = String(data.countryCode ?? '').toUpperCase()
    return /^[A-Z]{2}$/.test(countryCode) ? { ...coordinates, countryCode } : coordinates
  } catch {
    return null
  } finally {
    clearTimeout(timeoutId)
  }
}
