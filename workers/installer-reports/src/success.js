import { sendMessage } from './telegram.js'

export const SUCCESS_TEXT = {
  install: '🌬️ Iemand heeft Windpeek succesvol geïnstalleerd!',
  reinstall: '🌬️ Windpeek is succesvol opnieuw geïnstalleerd!',
  'update-firmware': '🌬️ Iemand heeft Windpeek succesvol bijgewerkt!',
}
const BOARDS = new Set(['seeedstudio_reterminal_e1001', 'seeedstudio_reterminal_e1002', 'seeedstudio_reterminal_e1003'])
const WEEK_MS = 7 * 24 * 60 * 60 * 1000

export function validSuccess(event) {
  return event && typeof event === 'object' && !Array.isArray(event) && Object.keys(event).length === 3 &&
    typeof event.eventId === 'string' && typeof event.action === 'string' &&
    /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(event.eventId) &&
    Object.hasOwn(SUCCESS_TEXT, event.action) && BOARDS.has(event.boardId)
}

// Legacy deployed namespace: retained so existing objects can finish their cleanup alarms.
// Reserve before sending: an uncertain Telegram response must not cause repeats.
export class InstallationNotification {
  constructor(ctx, env) {
    this.ctx = ctx
    this.env = env
  }

  async fetch(request) {
    const event = await request.json()
    if (!validSuccess(event)) return new Response(null, { status: 400 })
    const reserved = await this.ctx.blockConcurrencyWhile(async () => {
      if (await this.ctx.storage.get('attempted')) return false
      await this.ctx.storage.put('attempted', true)
      await this.ctx.storage.setAlarm(Date.now() + WEEK_MS)
      return true
    })
    if (!reserved) return new Response(null, { status: 204 })
    const sent = await sendMessage(this.env, SUCCESS_TEXT[event.action])
    return new Response(null, { status: sent ? 204 : 502 })
  }

  async alarm() {
    await this.ctx.storage.deleteAll()
  }
}
