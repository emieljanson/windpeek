const RETRYABLE = new Set([429, 500, 502, 503, 504])

export async function fetchPage(url, {
  fetcher = fetch,
  sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
  now = Date.now,
} = {}) {
  let lastError
  for (let attempt = 0; attempt < 3; attempt++) {
    await sleep(350)
    let response
    let body
    try {
      response = await fetcher(url, {
        headers: { 'User-Agent': 'Windpeek spot catalog (permission confirmed by project owner)' },
        signal: AbortSignal.timeout(30000),
      })
      body = await response.text()
    } catch (error) {
      lastError = error
      response = undefined
    }
    if (response?.ok) return body
    if (response && !RETRYABLE.has(response.status)) throw new Error(`HTTP ${response.status}: ${url}`)
    if (response) lastError = new Error(`HTTP ${response.status}`)
    const retryAfter = response?.headers.get('retry-after') ?? ''
    const seconds = Number(retryAfter)
    const delay = Number.isFinite(seconds) ? seconds * 1000 : Date.parse(retryAfter) - now()
    if (attempt < 2) await sleep(Math.max(3000 * (attempt + 1), Number.isFinite(delay) ? delay : 0))
  }
  throw new Error(`Retries exhausted: ${url}`, { cause: lastError })
}
