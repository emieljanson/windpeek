import { expect, it, vi } from 'vitest'
import { fetchPage } from '../scripts/spots/lib/fetch-page.mjs'

it.each(['12', 'Wed, 09 Sep 2026 12:00:12 GMT'])('honours Retry-After %s before retrying', async (retryAfter) => {
  const sleep = vi.fn()
  const fetcher = vi.fn()
    .mockResolvedValueOnce(new Response('', { status: 429, headers: { 'retry-after': retryAfter } }))
    .mockResolvedValueOnce(new Response('spots'))
  expect(await fetchPage('https://example.test', { fetcher, sleep, now: () => Date.parse('2026-09-09T12:00:00Z') })).toBe('spots')
  expect(sleep.mock.calls.map(([ms]) => ms)).toEqual([350, 12000, 350])
})

it('retries transport and body-read failures within the same bounded budget', async () => {
  const fetcher = vi.fn().mockRejectedValueOnce(new Error('connection reset'))
    .mockResolvedValueOnce({ ok: true, text: () => Promise.reject(new Error('body failed')) })
    .mockResolvedValueOnce(new Response('spots'))
  expect(await fetchPage('https://example.test', { fetcher, sleep: vi.fn() })).toBe('spots')
  expect(fetcher).toHaveBeenCalledTimes(3)
})

it('fails after three attempts and does not retry permanent HTTP errors', async () => {
  const fetcher = vi.fn().mockRejectedValue(new Error('offline'))
  await expect(fetchPage('https://example.test', { fetcher, sleep: vi.fn() })).rejects.toThrow('Retries exhausted')
  expect(fetcher).toHaveBeenCalledTimes(3)
  const permanent = vi.fn().mockResolvedValue(new Response('', { status: 404 }))
  await expect(fetchPage('https://example.test', { fetcher: permanent, sleep: vi.fn() })).rejects.toThrow('HTTP 404')
  expect(permanent).toHaveBeenCalledTimes(1)
})

it('stops on long server cooldowns without sleeping or retrying early', async () => {
  const sleep = vi.fn()
  const fetcher = vi.fn().mockResolvedValue(new Response('', { status: 429, headers: { 'retry-after': '99999999' } }))
  await expect(fetchPage('https://example.test', { fetcher, sleep })).rejects.toThrow('longer cooldown')
  expect(sleep.mock.calls).toEqual([[350]])
  expect(fetcher).toHaveBeenCalledTimes(1)
})

it('handles failure status without waiting for the response body', async () => {
  const text = vi.fn(() => { throw new Error('must not read error body') })
  const cancel = vi.fn()
  const fetcher = vi.fn().mockResolvedValueOnce({ ok: false, status: 503, headers: new Headers(), body: { cancel }, text })
    .mockResolvedValueOnce(new Response('spots'))
  expect(await fetchPage('https://example.test', { fetcher, sleep: vi.fn() })).toBe('spots')
  expect(text).not.toHaveBeenCalled()
  expect(cancel).toHaveBeenCalledTimes(1)
})
