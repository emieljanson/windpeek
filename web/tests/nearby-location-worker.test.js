import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { describe, expect, it } from 'vitest'

import worker from '../../workers/nearby-location/src/index.js'

const workerDirectory = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../workers/nearby-location',
)

const request = (method, cf) => ({ method, cf })

const expectPrivateCorsResponse = (response) => {
  expect(response.headers.get('cache-control')).toBe('no-store')
  expect(response.headers.get('access-control-allow-origin')).toBe('*')
}

describe('nearby location Worker', () => {
  it('returns only numeric coordinates from valid Cloudflare metadata', async () => {
    const response = await worker.fetch(request('GET', {
      latitude: '51.9225',
      longitude: 4.47917,
      city: 'Rotterdam',
      country: 'NL',
    }))

    expect(response.status).toBe(200)
    await expect(response.json()).resolves.toEqual({
      latitude: 51.9225,
      longitude: 4.47917,
    })
    expectPrivateCorsResponse(response)
  })

  it.each([
    ['missing metadata', undefined],
    ['missing longitude', { latitude: 51.9225 }],
    ['out-of-range latitude', { latitude: 91, longitude: 4.47917 }],
    ['out-of-range longitude', { latitude: 51.9225, longitude: 181 }],
    ['non-numeric coordinates', { latitude: 'north', longitude: 'east' }],
  ])('returns no location body for %s', async (_label, cf) => {
    const response = await worker.fetch(request('GET', cf))

    expect(response.status).toBe(204)
    expect(await response.text()).toBe('')
    expectPrivateCorsResponse(response)
  })

  it('answers preflight with CORS headers', async () => {
    const response = await worker.fetch(request('OPTIONS', {
      latitude: 51.9225,
      longitude: 4.47917,
    }))

    expect(response.status).toBe(204)
    expect(response.headers.get('access-control-allow-headers')).toBe('Content-Type')
    expect(response.headers.get('access-control-allow-methods')).toBe('GET, OPTIONS')
    expectPrivateCorsResponse(response)
  })

  it('rejects unsupported methods without exposing metadata', async () => {
    const response = await worker.fetch(request('POST', {
      latitude: 51.9225,
      longitude: 4.47917,
      city: 'Rotterdam',
    }))

    expect(response.status).toBe(405)
    expect(await response.text()).toBe('')
    expect(response.headers.get('allow')).toBe('GET, OPTIONS')
    expectPrivateCorsResponse(response)
  })

  it('disables observability, invocation logs, and logpush in Wrangler', () => {
    const wrangler = JSON.parse(fs.readFileSync(
      path.join(workerDirectory, 'wrangler.jsonc'),
      'utf8',
    ))

    expect(wrangler.observability).toEqual({
      enabled: false,
      logs: { invocation_logs: false },
    })
    expect(wrangler.logpush).toBe(false)
  })
})
