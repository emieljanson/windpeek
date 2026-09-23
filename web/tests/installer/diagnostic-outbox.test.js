import { describe, expect, it, vi } from 'vitest'
import { createDiagnosticOutbox } from '../../src/installer/diagnosticOutbox'

function storage() {
  const values = new Map()
  return { get length() { return values.size }, key: index => [...values.keys()][index],
    getItem: key => values.get(key), setItem: (key, value) => values.set(key, value),
    removeItem: key => values.delete(key) }

}
const sanitize = input => ({ occurrence: input.occurrence, snapshot: { code: input.snapshot.code } })
const input = { occurrence: 'attempt:1', snapshot: { code: 'connection-lost', password: 'must-not-store' } }

describe('automatic diagnostic outbox', () => {
  it('does not restore reports another tab has already delivered', async () => {
    const store = storage()
    const first = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize,
      reporter: { report: async () => ({ status: 'failed' }) } })
    await first.report(input)
    const second = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize,
      reporter: { report: async () => ({ status: 'sent', reference: 'WS-TEST123456' }) } })
    await vi.waitFor(() => expect(store.length).toBe(0))
    await first.report({ ...input, occurrence: 'new-report' })
    expect(store.length).toBe(1)
    expect(JSON.parse(store.getItem(store.key(0))).key).toBe('new-report')
    first.dispose(); second.dispose()
  })

  it('retains in-flight reports when the queue temporarily exceeds its cap', async () => {
    const store = storage()
    const finish = []
    const outbox = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize,
      reporter: { report: () => new Promise(resolve => finish.push(resolve)) } })
    const reports = Array.from({ length: 11 }, (_, index) => outbox.report({ ...input, occurrence: `report-${index}` }))
    await Promise.resolve()
    expect(store.length).toBe(11)
    for (const resolve of finish) resolve({ status: 'sent', reference: 'WS-TEST123456' })
    await Promise.all(reports)
    expect(store.length).toBe(0)
    outbox.dispose()
  })

  it('retains only sanitized reports and delivers them after a page reload', async () => {
    const store = storage()
    const first = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize,
      reporter: { report: vi.fn(async () => ({ status: 'failed' })) } })
    expect(await first.report(input)).toEqual({ status: 'queued' })
    expect(store.getItem(store.key(0))).not.toContain('must-not-store')
    first.dispose()
    const reporter = { report: vi.fn(async () => ({ status: 'sent', reference: 'WS-TEST123456' })) }
    const second = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize, reporter })
    await vi.waitFor(() => expect(store.length).toBe(0))
    expect(reporter.report).toHaveBeenCalledOnce()
    second.dispose()
  })

  it('retries automatically on connectivity returning and deduplicates concurrent sends', async () => {
    const store = storage()
    const target = new EventTarget()
    const reporter = { report: vi.fn().mockResolvedValueOnce({ status: 'failed' }).mockResolvedValue({ status: 'sent', reference: 'WS-TEST123456' }) }
    const outbox = createDiagnosticOutbox({ storage: () => store, eventTarget: target, sanitize, reporter })
    await Promise.all([outbox.report(input), outbox.report(input)])
    expect(reporter.report).toHaveBeenCalledOnce()
    target.dispatchEvent(new Event('online'))
    await vi.waitFor(() => expect(reporter.report).toHaveBeenCalledTimes(2))
    outbox.dispose()
  })

  it('preserves reports from another tab when one tab finishes', async () => {
    const store = storage()
    let resolve
    const first = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize,
      reporter: { report: () => new Promise(done => { resolve = done }) } })
    const second = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize,
      reporter: { report: async () => ({ status: 'failed' }) } })
    const delivery = first.report(input)
    await second.report({ ...input, occurrence: 'another-tab' })
    resolve({ status: 'sent', reference: 'WS-TEST123456' })
    await delivery
    expect(store.length).toBe(1)
    expect(JSON.parse(store.getItem(store.key(0))).key).toBe('another-tab')
    first.dispose(); second.dispose()
  })

  it('retries after a delay without requiring a click', async () => {
    const store = storage()
    vi.useFakeTimers()
    const reporter = { report: vi.fn().mockResolvedValue({ status: 'failed' }) }
    const outbox = createDiagnosticOutbox({ storage: () => store, eventTarget: new EventTarget(), sanitize, reporter })
    try {
      await outbox.report(input)
      await vi.advanceTimersByTimeAsync(30000)
      expect(reporter.report).toHaveBeenCalledTimes(2)
    } finally { outbox.dispose(); vi.useRealTimers() }
  })
})
