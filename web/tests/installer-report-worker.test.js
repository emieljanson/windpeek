// @vitest-environment node
import { describe, expect, it, vi } from 'vitest'
import { readFileSync } from 'node:fs'
import worker, { DSN as dsn } from '../../workers/installer-reports/src/index.js'

const body = JSON.stringify({ dsn, event_id: 'a'.repeat(32) }) + '\n{"type":"event"}\n{}'
const request = (options = {}) => new Request('https://reports.example/report', {
  method: 'POST', body, headers: { origin: 'https://windpeek.com', cookie: 'private-cookie', authorization: 'private-token' }, ...options,
})

describe('installer report relay', () => {
  it('disables invocation logs and observability', () => {
    const config = JSON.parse(readFileSync(new URL('../../workers/installer-reports/wrangler.jsonc', import.meta.url), 'utf8'))
    expect(config.observability).toEqual({ enabled: false, logs: { invocation_logs: false } })
    expect(config.logpush).toBe(false)
  })

  it('forwards the envelope unchanged to the fixed project without client credentials', async () => {
    const timeout = vi.spyOn(AbortSignal, 'timeout')
    const upstream = vi.fn(async () => new Response(null, { status: 200 }))
    const response = await worker.fetch(request(), {}, {}, upstream)
    expect(response.status).toBe(200)
    const [url, options] = upstream.mock.calls[0]
    expect(url).toBe('https://o4511992329535488.ingest.de.sentry.io/api/4511992357584976/envelope/')
    expect(new TextDecoder().decode(options.body)).toBe(body)
    expect(options.headers).toEqual({ 'Content-Type': 'application/x-sentry-envelope' })
    expect(options.redirect).toBe('manual')
    expect(options.signal).toBeInstanceOf(AbortSignal)
    expect(timeout).toHaveBeenCalledWith(4000)
    timeout.mockRestore()
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
