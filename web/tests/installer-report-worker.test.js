// @vitest-environment node
import { describe, expect, it, vi } from 'vitest'
import worker from '../../workers/installer-reports/src/index.js'

const dsn = 'https://8318e5c198b76f64d99701b0bab4a216@o4511992329535488.ingest.de.sentry.io/4511992357584976'
const body = JSON.stringify({ dsn, event_id: 'a'.repeat(32) }) + '\n{"type":"event"}\n{}'
const request = (options = {}) => new Request('https://reports.example/report', {
  method: 'POST', body, headers: { origin: 'https://windpeek.com', cookie: 'private-cookie', authorization: 'private-token' }, ...options,
})

describe('installer report relay', () => {
  it('forwards the envelope unchanged to the fixed project without client credentials', async () => {
    const upstream = vi.fn(async () => new Response(null, { status: 200 }))
    const response = await worker.fetch(request(), {}, {}, upstream)
    expect(response.status).toBe(200)
    const [url, options] = upstream.mock.calls[0]
    expect(url).toBe('https://o4511992329535488.ingest.de.sentry.io/api/4511992357584976/envelope/')
    expect(new TextDecoder().decode(options.body)).toBe(body)
    expect(options.headers).toEqual({ 'Content-Type': 'application/x-sentry-envelope' })
    expect(response.headers.get('Access-Control-Allow-Origin')).toBe('https://windpeek.com')
  })

  it.each([
    [{ headers: { origin: 'https://unrelated.example' } }, 403],
    [{ body: body.replace(dsn, 'https://attacker.example/1') }, 400],
    [{ body: 'not-an-envelope' }, 400],
    [{ body: 'x'.repeat(256 * 1024 + 1) }, 413],
  ])('rejects invalid or oversized requests without forwarding', async (options, status) => {
    const upstream = vi.fn()
    expect((await worker.fetch(request(options), {}, {}, upstream)).status).toBe(status)
    expect(upstream).not.toHaveBeenCalled()
  })

  it('preserves rate limits and never confirms an upstream failure', async () => {
    const response = await worker.fetch(request(), {}, {}, async () => new Response(null, {
      status: 429, headers: { 'Retry-After': '60', 'X-Sentry-Rate-Limits': '60:error:organization' },
    }))
    expect(response.status).toBe(429)
    expect(response.headers.get('Retry-After')).toBe('60')
    expect(response.headers.get('X-Sentry-Rate-Limits')).toBe('60:error:organization')
    expect((await worker.fetch(request(), {}, {}, async () => { throw new Error('offline') })).status).toBe(502)
  })

  it('handles browser preflight without sending a report', async () => {
    const upstream = vi.fn()
    const response = await worker.fetch(request({ method: 'OPTIONS', body: undefined }), {}, {}, upstream)
    expect(response.status).toBe(204)
    expect(upstream).not.toHaveBeenCalled()
  })
})
