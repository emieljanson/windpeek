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

    return normalizeCoordinates(await response.json())
  } catch {
    return null
  } finally {
    clearTimeout(timeoutId)
  }
}
