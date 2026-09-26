import { DSN, MAX_BYTES } from './constants.js'
import { validSuccess } from './success.js'
import { readBody, readJson } from './http.js'
import { botWebhook } from './botWebhook.js'
import { activityStub, recordActivity } from './projectActivity.js'
export { InstallationNotification } from './success.js'
export { ProjectActivity } from './projectActivity.js'

const INGEST = 'https://o4511992329535488.ingest.de.sentry.io/api/4511992357584976/envelope/'
const ALLOWED_ORIGINS = new Set(['https://windpeek.com', 'https://www.windpeek.com'])

function allowedOrigin(origin) {
  if (ALLOWED_ORIGINS.has(origin)) return true
  // Local installer verification uses synthetic reports in a real browser.
  try {
    const url = new URL(origin)
    return url.protocol === 'http:' && ['localhost', '127.0.0.1'].includes(url.hostname)
  } catch { return false }
}

export default {
  async scheduled(controller, env, context) {
    context.waitUntil(activityStub(env).fetch(new Request('https://activity/weekly', {
      method: 'POST', body: JSON.stringify({ scheduledTime: controller.scheduledTime }),
    })).then(response => { if (!response.ok) throw new Error('Weekly report delivery failed') }))
  },
  async fetch(request, env, context, fetchUpstream = fetch) {
    const path = new URL(request.url).pathname
    if (path === '/telegram') return botWebhook(request, env)
    const origin = request.headers.get('origin')
    const headers = { 'Cache-Control': 'no-store', Vary: 'Origin' }
    const reply = (status) => new Response(null, { status, headers })
    if (!allowedOrigin(origin)) return reply(403)
    headers['Access-Control-Allow-Origin'] = origin
    headers['Access-Control-Allow-Headers'] = 'Content-Type'
    headers['Access-Control-Allow-Methods'] = 'POST, OPTIONS'
    headers['Access-Control-Expose-Headers'] = 'X-Sentry-Rate-Limits, Retry-After'
    if (!['/report', '/success', '/visit'].includes(path)) return reply(404)
    // Local QA must never produce real owner notifications.
    if (path !== '/report' && !ALLOWED_ORIGINS.has(origin)) return reply(403)
    if (request.method === 'OPTIONS') return reply(204)
    if (request.method !== 'POST') return reply(405)
    if (path !== '/report') {
      if (!env.PROJECT_ACTIVITY || !env.SUCCESS_RATE_LIMITER || !env.VISIT_RATE_LIMITER) return reply(503)
      try {
        const event = await readJson(request, 512)
        if (!event) return reply(413)
        const visit = path === '/visit'
        if (visit ? !validVisit(event) : !validSuccess(event)) return reply(400)
        // Rate limiting is abuse mitigation, not proof of a genuine install.
        const limiter = visit ? env.VISIT_RATE_LIMITER : env.SUCCESS_RATE_LIMITER
        const key = visit ? request.headers.get('CF-Connecting-IP') || 'unknown' : 'installation-notifications'
        const { success } = await limiter.limit({ key })
        if (!success) return reply(429)
        const response = await recordActivity(env, { eventId: event.eventId, action: visit ? 'visit' : event.action })
        return reply(response.status)
      } catch (error) { return reply(error instanceof SyntaxError ? 400 : 502) }
    }
    let body
    let eventId
    try {
      body = await readBody(request, MAX_BYTES)
      if (!body) return reply(413)
      const newline = body.indexOf(10)
      if (newline < 1 || newline > 4096) return reply(400)
      const envelope = JSON.parse(new TextDecoder().decode(body.subarray(0, newline)))
      if (envelope.dsn !== DSN || !/^[a-f0-9]{32}$/i.test(envelope.event_id)) return reply(400)
      eventId = envelope.event_id
    } catch { return reply(400) }
    try {
      // Fixed destination, no client cookies, authorization or IP forwarding.
      const response = await fetchUpstream(INGEST, {
        method: 'POST', headers: { 'Content-Type': 'application/x-sentry-envelope' }, body,
        signal: AbortSignal.timeout(4000), redirect: 'manual',
      })
      for (const name of ['X-Sentry-Rate-Limits', 'Retry-After']) {
        if (response.headers.has(name)) headers[name] = response.headers.get(name)
      }
      if (response.ok && env.PROJECT_ACTIVITY && ALLOWED_ORIGINS.has(origin)) {
        context?.waitUntil?.(Promise.resolve().then(() => recordActivity(env, { eventId, action: 'failure' })).catch(() => {}))
      }
      return reply(response.status)
    } catch { return reply(502) }
  },
}

function validVisit(event) {
  return typeof event === 'object' && Object.keys(event).length === 1 &&
    typeof event.eventId === 'string' && /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(event.eventId)
}
