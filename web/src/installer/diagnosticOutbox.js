const STORAGE_PREFIX = 'windpeek.pending-diagnostic.v1:'
const MAX_REPORTS = 10
const MAX_AGE_MS = 7 * 24 * 60 * 60 * 1000
const RETRY_MS = 30000

// Only the supplied, privacy-filtered payload is persisted. Delivery confirmation
// is the only success signal; closing the installer does not discard the queue.
export function createDiagnosticOutbox({ reporter, sanitize, isConfirmed = result => result?.status === 'sent' && /^WS-[0-9A-HJKMNP-TV-Z]{10}$/.test(result.reference), enabled = true,
  storage = () => globalThis.localStorage, eventTarget = globalThis.window,
  now = () => Date.now(), setTimer = setTimeout, clearTimer = clearTimeout,
} = {}) {
  const pending = new Map()
  const inFlight = new Map()
  const listeners = new Map()
  const persisted = new Set()
  let timer
  let disposed = false

  function remove(key) {
    pending.delete(key)
    persisted.delete(key)
    try { storage()?.removeItem(STORAGE_PREFIX + key) } catch {}
  }

  function prune() {
    for (const [key, entry] of pending) if (!inFlight.has(key) && now() - entry.createdAt > MAX_AGE_MS) remove(key)
    for (const key of pending.keys()) {
      if (pending.size <= MAX_REPORTS) break
      if (!inFlight.has(key)) remove(key)
    }
  }

  function persist() {
    prune()
    for (const key of listeners.keys()) if (!pending.has(key) && !inFlight.has(key)) listeners.delete(key)
    // One key per report: concurrent tabs cannot overwrite each other's queue.
    for (const [key, entry] of pending) {
      if (persisted.has(key)) continue
      try {
        const store = storage()
        if (store) { store.setItem(STORAGE_PREFIX + key, JSON.stringify(entry)); persisted.add(key) }
      } catch {}
    }
  }

  function schedule() {
    if (disposed || timer || !pending.size) return
    timer = setTimer(() => { timer = null; void flush() }, RETRY_MS)
  }

  function deliver(key) {
    if (inFlight.has(key)) return inFlight.get(key)
    // A different tab may already have confirmed this persisted report.
    try {
      if (persisted.has(key) && storage()?.getItem(STORAGE_PREFIX + key) == null) remove(key)
    } catch {}
    const entry = pending.get(key)
    if (!entry || disposed) return Promise.resolve({ status: 'failed' })
    const delivery = Promise.resolve().then(() => reporter.report(entry.input))
      .catch(() => ({ status: 'failed' }))
      .then(result => {
        const outcome = isConfirmed(result) ? result : { status: 'queued' }
        if (outcome.status === 'sent') remove(key)
        persist()
        try { listeners.get(key)?.(outcome) } catch {}
        if (outcome.status === 'sent') listeners.delete(key)
        return outcome
      })
      .finally(() => { inFlight.delete(key); persist(); schedule() })
    inFlight.set(key, delivery)
    return delivery
  }

  async function flush() {
    if (!enabled || disposed) return
    prune()
    // Sequential to avoid flooding the reporting endpoint when connectivity returns.
    for (const key of [...pending.keys()]) await deliver(key)
  }

  if (enabled) {
    try {
      const store = storage()
      for (let index = 0; index < (store?.length ?? 0); index++) {
        const storedKey = store.key(index)
        if (!storedKey?.startsWith(STORAGE_PREFIX)) continue
        try {
          const entry = JSON.parse(store.getItem(storedKey))
          if (typeof entry?.key !== 'string' || storedKey !== STORAGE_PREFIX + entry.key || !Number.isFinite(entry.createdAt)) continue
          const input = sanitize(entry.input)
          if (input && input.occurrence === entry.key) {
            pending.set(entry.key, { key: entry.key, createdAt: entry.createdAt, input })
            persisted.add(entry.key)
          }
        } catch {}
      }
    } catch {}
    prune()
    eventTarget?.addEventListener('online', flush)
    if (pending.size) void flush()
  }

  return {
    report(input, onDelivery) {
      if (!enabled || disposed) return Promise.resolve({ status: 'failed' })
      const safe = sanitize(input)
      if (!safe) return Promise.resolve({ status: 'failed' })
      const key = String(safe.occurrence)
      if (onDelivery) listeners.set(key, onDelivery)
      if (!pending.has(key)) pending.set(key, { key, createdAt: now(), input: safe })
      const delivery = deliver(key)
      persist()
      return delivery
    },
    dispose() {
      disposed = true
      clearTimer(timer)
      eventTarget?.removeEventListener('online', flush)
    },
  }
}
