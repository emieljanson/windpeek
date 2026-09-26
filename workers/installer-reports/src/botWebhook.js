import { readJson } from './http.js'
import { ownerCommand } from './telegram.js'
import { activityStub } from './projectActivity.js'

export async function botWebhook(request, env) {
  const reply = status => new Response(null, { status })
  if (request.method !== 'POST') return reply(405)
  if (!env.TELEGRAM_WEBHOOK_SECRET || !env.TELEGRAM_CHAT_ID || !env.PROJECT_ACTIVITY) return reply(503)
  if (request.headers.get('X-Telegram-Bot-Api-Secret-Token') !== env.TELEGRAM_WEBHOOK_SECRET) return reply(403)
  try {
    const update = await readJson(request, 16384)
    if (!update) return reply(413)
    const command = ownerCommand(update, env.TELEGRAM_CHAT_ID)
    if (!command) return reply(204)
    if (!(await env.SUCCESS_RATE_LIMITER.limit({ key: 'owner-commands' })).success) return reply(204)
    return await activityStub(env).fetch(new Request('https://activity/command', { method: 'POST', body: JSON.stringify(command) }))
  } catch (error) { return reply(error instanceof SyntaxError ? 400 : 502) }
}
