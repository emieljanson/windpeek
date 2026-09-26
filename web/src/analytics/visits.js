const SESSION_KEY = 'windpeek-visit'
const SESSION_MS = 30 * 60 * 1000

export async function reportVisit({
  endpoint = import.meta.env.VITE_INSTALLER_REPORT_URL,
  location = globalThis.location, navigator = globalThis.navigator,
  fetchImpl = globalThis.fetch, now = Date.now(),
} = {}) {
  if (!endpoint || !['windpeek.com', 'www.windpeek.com'].includes(location?.hostname) ||
      navigator?.doNotTrack === '1' || navigator?.globalPrivacyControl === true) return
  try {
    const stored = JSON.parse(sessionStorage.getItem(SESSION_KEY) || 'null')
    if (stored && Number.isFinite(stored.started) && now >= stored.started && now - stored.started < SESSION_MS) return
    const eventId = crypto.randomUUID()
    // No persistent visitor identity. Storage denial simply disables measurement.
    sessionStorage.setItem(SESSION_KEY, JSON.stringify({ started: now }))
    await fetchImpl(new URL('/visit', endpoint).href, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ eventId }), credentials: 'omit', keepalive: true,
      redirect: 'error', signal: AbortSignal.timeout(4000),
    })
  } catch { /* Optional analytics never interferes with the website. */ }
}
