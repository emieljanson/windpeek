// @vitest-environment node
import { describe, expect, it, vi } from 'vitest'
import worker from '../../workers/installer-reports/src/index.js'

const event = { eventId: '12345678-1234-4123-8123-123456789abc', action: 'install', boardId: 'seeedstudio_reterminal_e1002' }
const request = (data = event, origin = 'https://windpeek.com', path = '/success') => new Request(`https://relay.example${path}`, {
  method: 'POST', headers: { origin }, body: JSON.stringify(data),
})
const setup = () => {
  const receive = vi.fn(async () => new Response(null, { status: 204 }))
  const limit = vi.fn(async () => ({ success: true }))
  return { receive, env: { PROJECT_ACTIVITY: { idFromName: id => id, get: () => ({ fetch: receive }) },
    SUCCESS_RATE_LIMITER: { limit }, VISIT_RATE_LIMITER: { limit } } }
}

describe('public activity ingestion', () => {
  it.each([null, false, 0, '', []])('rejects invalid JSON event shapes with 400: %j', async data => {
    for (const path of ['/success', '/visit']) {
      const { env, receive } = setup()
      expect((await worker.fetch(request(data, 'https://windpeek.com', path), env)).status).toBe(400)
      expect(receive).not.toHaveBeenCalled()
    }
  })
  it('reports missing webhook rate limiter as unavailable', async () => {
    const { env } = setup()
    delete env.SUCCESS_RATE_LIMITER
    Object.assign(env, { TELEGRAM_WEBHOOK_SECRET: 'test', TELEGRAM_CHAT_ID: '123' })
    expect((await worker.fetch(request({}, 'https://windpeek.com', '/telegram'), env)).status).toBe(503)
  })
  it('forwards only validated metadata to the ledger', async () => {
    const { env, receive } = setup()
    expect((await worker.fetch(request(), env)).status).toBe(204)
    expect(new URL(receive.mock.calls[0][0].url).pathname).toBe('/event')
    expect(await receive.mock.calls[0][0].json()).toEqual({ eventId: event.eventId, action: 'install' })
  })
  it.each([
    [{ ...event, password: 'private' }, 'https://windpeek.com', 400],
    [{ ...event, action: 'update-configuration' }, 'https://windpeek.com', 400],
    [{ ...event, action: 'toString' }, 'https://windpeek.com', 400],
    [{ ...event, boardId: 'unknown' }, 'https://windpeek.com', 400],
    [{ ...event, eventId: [event.eventId] }, 'https://windpeek.com', 400],
    [{ ...event, action: ['install'] }, 'https://windpeek.com', 400],
    [event, 'http://localhost:4174', 403],
    [event, 'https://other.example', 403],
    ['x'.repeat(513), 'https://windpeek.com', 413],
  ])('rejects invalid and nonproduction requests', async (data, origin, status) => {
    const { env, receive } = setup()
    expect((await worker.fetch(request(data, origin), env)).status).toBe(status)
    expect(receive).not.toHaveBeenCalled()
  })
  it('rejects visits that include user metadata', async () => {
    const { env, receive } = setup()
    expect((await worker.fetch(request({ eventId: event.eventId, user: 'private' }, 'https://windpeek.com', '/visit'), env)).status).toBe(400)
    expect(receive).not.toHaveBeenCalled()
  })
  it('fails closed when unconfigured or rate limited', async () => {
    const { env, receive } = setup()
    expect((await worker.fetch(request(), {})).status).toBe(503)
    env.SUCCESS_RATE_LIMITER.limit.mockResolvedValue({ success: false })
    expect((await worker.fetch(request(), env)).status).toBe(429)
    expect(receive).not.toHaveBeenCalled()
  })
  it('allows production preflight without requiring secrets', async () => {
    const response = await worker.fetch(new Request('https://relay.example/success', {
      method: 'OPTIONS', headers: { origin: 'https://windpeek.com' },
    }), {})
    expect(response.status).toBe(204)
    expect(response.headers.get('Access-Control-Allow-Origin')).toBe('https://windpeek.com')
    expect(response.headers.get('Access-Control-Allow-Methods')).toBe('POST, OPTIONS')
    expect(response.headers.get('Access-Control-Allow-Headers')).toBe('Content-Type')
  })
  it('dispatches weekly cron and propagates delivery failures', async () => {
    const { env, receive } = setup()
    const tasks = []
    await worker.scheduled({ scheduledTime: 123 }, env, { waitUntil: p => tasks.push(p) })
    await Promise.all(tasks)
    expect(new URL(receive.mock.calls[0][0].url).pathname).toBe('/weekly')
    expect(await receive.mock.calls[0][0].json()).toEqual({ scheduledTime: 123 })
    receive.mockResolvedValue(new Response(null, { status: 502 }))
    await worker.scheduled({ scheduledTime: 123 }, env, { waitUntil: p => tasks.push(p) })
    await expect(tasks.at(-1)).rejects.toThrow('Weekly report delivery failed')
  })
})
