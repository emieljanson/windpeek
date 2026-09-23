export const DSN = 'https://8318e5c198b76f64d99701b0bab4a216@o4511992329535488.ingest.de.sentry.io/4511992357584976'
const INGEST = 'https://o4511992329535488.ingest.de.sentry.io/api/4511992357584976/envelope/'
export const MAX_BYTES = 2 * 1024 * 1024
const ALLOWED_ORIGINS = new Set(['https://windpeek.com', 'https://www.windpeek.com'])

function allowedOrigin(origin) {
  if (ALLOWED_ORIGINS.has(origin)) return true
  // Local installer verification uses synthetic reports in a real browser.
  try {
    const url = new URL(origin)
    return url.protocol === 'http:' && ['localhost', '127.0.0.1'].includes(url.hostname)
  } catch { return false }
}

async function readEnvelope(request) {
  if (Number(request.headers.get('content-length')) > MAX_BYTES) return null
  const reader = request.body?.getReader()
  if (!reader) return null
  const chunks = []
  let size = 0
  try {
    while (true) {
      const { value, done } = await reader.read()
      if (done) break
      size += value.byteLength
      if (size > MAX_BYTES) { await reader.cancel(); return null }
      chunks.push(value)
    }
  } finally { reader.releaseLock() }
  const body = new Uint8Array(size)
  let offset = 0
  for (const chunk of chunks) { body.set(chunk, offset); offset += chunk.byteLength }
  return body
}

export default {
  async fetch(request, _env, _context, fetchUpstream = fetch) {
    const origin = request.headers.get('origin')
    const headers = { 'Cache-Control': 'no-store', Vary: 'Origin' }
    const reply = (status) => new Response(null, { status, headers })
    if (!allowedOrigin(origin)) return reply(403)
    headers['Access-Control-Allow-Origin'] = origin
    headers['Access-Control-Allow-Headers'] = 'Content-Type'
    headers['Access-Control-Allow-Methods'] = 'POST, OPTIONS'
    headers['Access-Control-Expose-Headers'] = 'X-Sentry-Rate-Limits, Retry-After'
    if (new URL(request.url).pathname !== '/report') return reply(404)
    if (request.method === 'OPTIONS') return reply(204)
    if (request.method !== 'POST') return reply(405)
    let body
    try {
      body = await readEnvelope(request)
      if (!body) return reply(413)
      const newline = body.indexOf(10)
      if (newline < 1 || newline > 4096) return reply(400)
      const envelope = JSON.parse(new TextDecoder().decode(body.subarray(0, newline)))
      if (envelope.dsn !== DSN || !/^[a-f0-9]{32}$/i.test(envelope.event_id)) return reply(400)
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
      return reply(response.status)
    } catch { return reply(502) }
  },
}
