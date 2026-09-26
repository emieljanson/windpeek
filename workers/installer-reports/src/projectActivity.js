import { SUCCESS_TEXT } from './success.js'
import { sendMessage, HELP } from './telegram.js'
import { activityReport, DAY_MS, WEEK_MS } from './activityReport.js'

const MILESTONES = new Set([10, 25, 50, 100, 250, 500, 1000])

export function activityStub(env) {
  return env.PROJECT_ACTIVITY.get(env.PROJECT_ACTIVITY.idFromName('windpeek'))
}

export function recordActivity(env, event) {
  return activityStub(env).fetch(new Request('https://activity/event', { method: 'POST', body: JSON.stringify(event) }))
}

export class ProjectActivity {
  constructor(ctx, env) {
    this.ctx = ctx
    this.env = env
    this.sql = ctx.storage.sql
    this.sql.exec(`CREATE TABLE IF NOT EXISTS events (id TEXT PRIMARY KEY, action TEXT NOT NULL, created INTEGER NOT NULL);
      CREATE INDEX IF NOT EXISTS events_time ON events(created);
      CREATE TABLE IF NOT EXISTS state (key TEXT PRIMARY KEY, value INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS messages (id TEXT PRIMARY KEY, created INTEGER NOT NULL);`)
    this.sql.exec('INSERT OR IGNORE INTO state VALUES (?, ?)', 'startedAt', Date.now())
  }

  counts(start, end) {
    return Object.fromEntries(this.sql.exec('SELECT action, COUNT(*) AS count FROM events WHERE created >= ? AND created < ? GROUP BY action', start, end).toArray().map(row => [row.action, row.count]))
  }

  state(key) { return this.sql.exec('SELECT value FROM state WHERE key = ?', key).toArray()[0]?.value || 0 }

  reserveMessage(id, now) {
    return this.sql.exec('INSERT OR IGNORE INTO messages VALUES (?, ?) RETURNING id', id, now).toArray().length > 0
  }

  async notify(id, text, now) {
    // At most one attempt: Telegram has no idempotency key for sendMessage.
    if (!this.reserveMessage(id, now)) return true
    return sendMessage(this.env, text)
  }

  async event(event, now) {
    const result = this.ctx.storage.transactionSync(() => {
      const inserted = this.sql.exec('INSERT OR IGNORE INTO events VALUES (?, ?, ?) RETURNING id', `${event.action}:${event.eventId}`, event.action, now).toArray().length
      if (!inserted) return null
      if (event.action === 'install') {
        this.sql.exec("INSERT INTO state VALUES ('installations', 1) ON CONFLICT(key) DO UPDATE SET value = value + 1")
      }
      return { total: this.state('installations'), failures: this.counts(now - 3600000, now + 1).failure || 0 }
    })
    if (!result) return true
    if (SUCCESS_TEXT[event.action]) {
      const milestone = event.action === 'install' && MILESTONES.has(result.total)
        ? `\n\n🎉 Mijlpaal: ${result.total} installaties sinds de start van de meting!` : ''
      return this.notify(`event:${event.eventId}`, SUCCESS_TEXT[event.action] + milestone, now)
    }
    if (event.action === 'failure' && result.failures >= 3) {
      if (now - this.state('lastErrorAlert') < 3600000) return true
      this.sql.exec("INSERT INTO state VALUES ('lastErrorAlert', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value", now)
      return sendMessage(this.env, '🛠 Even naar Windpeek kijken?\nEr zijn minstens 3 installatiefoutrapporten binnen een uur ontvangen. Details staan in Sentry.')
    }
    return true
  }

  report(now) {
    return activityReport({ current: this.counts(now - WEEK_MS, now),
      previous: this.counts(now - 2 * WEEK_MS, now - WEEK_MS),
      total: this.state('installations'), startedAt: this.state('startedAt'), now,
    })
  }

  async fetch(request) {
    const now = Date.now()
    const body = await request.json()
    const path = new URL(request.url).pathname
    let sent
    if (path === '/event') sent = await this.event(body, now)
    else if (path === '/command') {
      // Reserve before querying remote analytics to coalesce Telegram retries.
      if (!this.reserveMessage(`command:${body.updateId}`, now)) return new Response(null, { status: 204 })
      const text = body.command === 'stats' ? this.report(now) : HELP
      sent = await sendMessage(this.env, text)
    } else if (path === '/weekly') {
      this.sql.exec('DELETE FROM events WHERE created < ?', now - 21 * DAY_MS)
      this.sql.exec('DELETE FROM messages WHERE created < ?', now - 21 * DAY_MS)
      const slot = new Date(body.scheduledTime).toISOString().slice(0, 10)
      if (!this.reserveMessage(`weekly:${slot}`, now)) return new Response(null, { status: 204 })
      sent = await sendMessage(this.env, this.report(body.scheduledTime))
    } else return new Response(null, { status: 404 })
    return new Response(null, { status: sent ? 204 : 502 })
  }
}
