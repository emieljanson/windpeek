import { beforeEach, describe, expect, it, vi } from 'vitest'
import { reportVisit } from '../src/analytics/visits'
import { activityReport, WEEK_MS } from '../../workers/installer-reports/src/activityReport.js'
import { ownerCommand } from '../../workers/installer-reports/src/telegram.js'

beforeEach(() => sessionStorage.clear())
describe('anonymous visits', () => {
  const options = () => ({ endpoint: 'https://reports.example/report', location: { hostname: 'windpeek.com' }, navigator: {}, now: 10000000,
    fetchImpl: vi.fn(async () => new Response(null, { status: 204 })) })
  it('counts a tab session once, and starts a new visit after 30 minutes', async () => {
    const opts = options()
    await reportVisit(opts)
    await reportVisit(opts)
    expect(opts.fetchImpl).toHaveBeenCalledOnce()
    expect(Object.keys(JSON.parse(opts.fetchImpl.mock.calls[0][1].body))).toEqual(['eventId'])
    await reportVisit({ ...opts, now: opts.now + 1800000 })
    expect(opts.fetchImpl).toHaveBeenCalledTimes(2)
  })
  it.each([{ doNotTrack: '1' }, { globalPrivacyControl: true }])('respects privacy preference %j', async navigator => {
    const opts = options()
    await reportVisit({ ...opts, navigator })
    expect(opts.fetchImpl).not.toHaveBeenCalled()
  })
  it('does not count localhost', async () => {
    const opts = options()
    await reportVisit({ ...opts, location: { hostname: 'localhost' } })
    expect(opts.fetchImpl).not.toHaveBeenCalled()
  })
})

describe('bot boundaries and honest summaries', () => {
  const update = { update_id: 1, message: { chat: { id: 123, type: 'private' }, from: { id: 123 }, text: '/stats' } }
  it('accepts only the private owner chat and sender', () => {
    expect(ownerCommand(update, '123')).toEqual({ updateId: 1, command: 'stats' })
    expect(ownerCommand(update, '456')).toBeNull()
    expect(ownerCommand({ ...update, message: { ...update.message, from: { id: 456 } } }, '123')).toBeNull()
    expect(ownerCommand({ ...update, message: { ...update.message, chat: { id: 123, type: 'group' } } }, '123')).toBeNull()
  })
  it('does not compare a partial baseline with a full week', () => {
    const now = Date.UTC(2026, 8, 27)
    const report = activityReport({ current: { install: 3, visit: 42 }, previous: {}, total: 3, startedAt: now - 1000, now })
    expect(report).toContain('Websitebezoeken: 42')
    expect(report).not.toContain('t.o.v. vorige week')
    expect(activityReport({ current: {}, previous: { visit: 10 }, total: 0, startedAt: now - 3 * WEEK_MS, now })).toContain('Websitebezoeken: 0 (-10 t.o.v. vorige week)')
  })
})
