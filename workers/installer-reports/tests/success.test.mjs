import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { randomUUID } from 'node:crypto'
import { Miniflare, convertV4MiniflareOptions, Response } from 'miniflare'

async function setup() {
  const config = JSON.parse(readFileSync(new URL('../wrangler.jsonc', import.meta.url), 'utf8'))
  const messages = []
  const state = { failTelegram: false }
  const runtime = new Miniflare(convertV4MiniflareOptions({
    name: config.name, modules: true, compatibilityDate: config.compatibility_date,
    script: readFileSync(new URL('../.wrangler/test-build/index.js', import.meta.url), 'utf8'),
    bindings: { TELEGRAM_BOT_TOKEN: 'test-token', TELEGRAM_CHAT_ID: '123', TELEGRAM_WEBHOOK_SECRET: 'test-secret' },
    durableObjects: Object.fromEntries(config.durable_objects.bindings.map(binding => [binding.name, { className: binding.class_name, useSQLite: true }])),
    ratelimits: Object.fromEntries(config.ratelimits.map(({ name, ...options }) => [name, options])),
    outboundService: async request => {
      assert.equal(request.url, 'https://api.telegram.org/bottest-token/sendMessage')
      const body = await request.json()
      assert.equal(body.chat_id, '123')
      messages.push(body.text)
      return new Response(JSON.stringify({ ok: !state.failTelegram }))
    },
  }))
  const send = (path, body, headers = { origin: 'https://windpeek.com' }) => runtime.dispatchFetch(`https://relay.example${path}`, { method: 'POST', headers, body: JSON.stringify(body) })
  const command = (id, text = '/stats', owner = 123, secret = 'test-secret') => send('/telegram', { update_id: id, message: { chat: { id: owner, type: 'private' }, from: { id: owner }, text } }, { 'X-Telegram-Bot-Api-Secret-Token': secret })
  const bindings = await runtime.getBindings()
  const ledger = bindings.PROJECT_ACTIVITY.get(bindings.PROJECT_ACTIVITY.idFromName('windpeek'))
  const internal = (path, body) => ledger.fetch(`https://activity${path}`, { method: 'POST', body: JSON.stringify(body) })
  return { runtime, messages, state, send, command, internal }
}
const install = () => ({ eventId: randomUUID(), action: 'install', boardId: 'seeedstudio_reterminal_e1002' })

test('concurrent completions count and notify once', async () => {
  const s = await setup()
  try {
    const event = install()
    const responses = await Promise.all([1, 2, 3].map(() => s.send('/success', event)))
    assert.deepEqual(responses.map(r => r.status), [204, 204, 204])
    assert.equal(s.messages.length, 1)
    assert.equal((await s.command(1)).status, 204)
    assert.match(s.messages[1], /Installaties: 1/)
  } finally { await s.runtime.dispose() }
})
test('webhook authenticates owner and coalesces retries', async () => {
  const s = await setup()
  try {
    assert.equal((await s.command(1, '/stats', 123, 'wrong')).status, 403)
    assert.equal((await s.command(2, '/stats', 456)).status, 204)
    assert.equal(s.messages.length, 0)
    const responses = await Promise.all([s.command(3), s.command(3)])
    assert.deepEqual(responses.map(r => r.status), [204, 204])
    assert.equal(s.messages.length, 1)
    await s.command(4, '/help')
    assert.match(s.messages[1], /Alleen jij/)
  } finally { await s.runtime.dispose() }
})
test('visits count once without immediate messages', async () => {
  const s = await setup()
  try {
    const event = { eventId: randomUUID() }
    await s.send('/visit', event)
    await s.send('/visit', event)
    assert.equal(s.messages.length, 0)
    await s.command(1)
    assert.match(s.messages[0], /Websitebezoeken: 1/)
  } finally { await s.runtime.dispose() }
})
test('Telegram failure preserves count without retrying notification', async () => {
  const s = await setup()
  try {
    const event = install()
    s.state.failTelegram = true
    assert.equal((await s.send('/success', event)).status, 502)
    assert.equal((await s.send('/success', event)).status, 204)
    assert.equal(s.messages.length, 1)
    s.state.failTelegram = false
    await s.command(1)
    assert.match(s.messages[1], /Installaties: 1/)
  } finally { await s.runtime.dispose() }
})
test('milestones ignore replayed events', async () => {
  const s = await setup()
  try {
    for (let i = 0; i < 10; i++) {
      const event = install()
      await s.internal('/event', event)
      await s.internal('/event', event)
    }
    assert.equal(s.messages.length, 10)
    assert.equal(s.messages.filter(text => text.includes('Mijlpaal: 10')).length, 1)
  } finally { await s.runtime.dispose() }
})
test('groups errors and deduplicates weekly schedule', async () => {
  const s = await setup()
  try {
    for (let i = 0; i < 4; i++) await s.internal('/event', { eventId: randomUUID(), action: 'failure' })
    assert.equal(s.messages.length, 1)
    const body = { scheduledTime: Date.now() + 1 }
    const responses = await Promise.all([s.internal('/weekly', body), s.internal('/weekly', body)])
    assert.deepEqual(responses.map(r => r.status), [204, 204])
    assert.equal(s.messages.length, 2)
    assert.match(s.messages[1], /Foutrapporten: 4/)
  } finally { await s.runtime.dispose() }
})
