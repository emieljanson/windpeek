import { afterEach, describe, expect, it, vi } from 'vitest'

import { fetchIpLocation, IP_LOCATION_TIMEOUT_MS } from '../src/location/ipLocation.js'

afterEach(() => {
  vi.useRealTimers()
})

describe('fetchIpLocation', () => {
  it('fetches a configured endpoint with GET and returns valid coordinates', async () => {
    const fetcher = vi.fn().mockResolvedValue(new Response(JSON.stringify({
      latitude: 51.5,
      longitude: -0.12,
    }), { status: 200 }))

    await expect(fetchIpLocation({ endpoint: 'https://location.example.test', fetcher }))
      .resolves.toEqual({ latitude: 51.5, longitude: -0.12 })
    expect(fetcher).toHaveBeenCalledOnce()
    expect(fetcher).toHaveBeenCalledWith('https://location.example.test', expect.objectContaining({
      method: 'GET',
      cache: 'no-store',
      credentials: 'omit',
      signal: expect.any(AbortSignal),
    }))
  })

  it('preserves a valid country code for the initial temperature unit', async () => {
    const fetcher = async () => new Response(JSON.stringify({ latitude: 40, longitude: -74, countryCode: 'us', city: 'ignored' }))
    await expect(fetchIpLocation({ endpoint: '/location', fetcher })).resolves.toEqual({ latitude: 40, longitude: -74, countryCode: 'US' })
  })

  it('does not make a request when the endpoint is missing', async () => {
    const fetcher = vi.fn()

    await expect(fetchIpLocation({ endpoint: '', fetcher })).resolves.toBeNull()
    expect(fetcher).not.toHaveBeenCalled()
  })

  it.each([
    ['a non-success response', () => Promise.resolve(new Response(null, { status: 503 }))],
    ['malformed JSON', () => Promise.resolve(new Response('{', { status: 200 }))],
    ['invalid coordinates', () => Promise.resolve(new Response(JSON.stringify({ latitude: 100, longitude: 4 }), { status: 200 }))],
    ['a network error', () => Promise.reject(new Error('offline'))],
    ['an abort', () => Promise.reject(new DOMException('Aborted', 'AbortError'))],
  ])('silently returns no location for %s', async (_label, response) => {
    await expect(fetchIpLocation({ endpoint: '/location', fetcher: vi.fn(response) }))
      .resolves.toBeNull()
  })

  it('aborts after three seconds and silently returns no location', async () => {
    vi.useFakeTimers()
    const fetcher = vi.fn((_url, { signal }) => new Promise((_resolve, reject) => {
      signal.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')))
    }))

    const result = fetchIpLocation({ endpoint: '/location', fetcher })
    await vi.advanceTimersByTimeAsync(IP_LOCATION_TIMEOUT_MS)

    await expect(result).resolves.toBeNull()
    expect(fetcher.mock.calls[0][1].signal.aborted).toBe(true)
    expect(IP_LOCATION_TIMEOUT_MS).toBe(3_000)
  })
})
