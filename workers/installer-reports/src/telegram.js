export async function sendMessage(env, text) {
  if (!env.TELEGRAM_BOT_TOKEN || !env.TELEGRAM_CHAT_ID) return false
  try {
    const response = await fetch(`https://api.telegram.org/bot${env.TELEGRAM_BOT_TOKEN}/sendMessage`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ chat_id: env.TELEGRAM_CHAT_ID, text,
        reply_markup: { keyboard: [[{ text: '/stats' }, { text: '/help' }]], resize_keyboard: true },
      }),
      signal: AbortSignal.timeout(4000), redirect: 'manual',
    })
    return response.ok && (await response.json()).ok === true
  } catch { return false }
}

export const HELP = '🌬️ Windpeek · jouw project in beweging\n\n/stats — de afgelopen 7 dagen\n/help — dit overzicht\n\nJe krijgt een bericht bij installaties en mijlpalen, en elke maandagochtend een weekoverzicht. Bij meerdere foutrapporten kort na elkaar krijg je één waarschuwing.\n\nAlleen jij kunt deze bot gebruiken.'

export function ownerCommand(update, ownerId) {
  const message = update?.message
  if (!Number.isSafeInteger(update?.update_id) || update.update_id < 0 ||
      message?.chat?.type !== 'private' || String(message.chat.id) !== ownerId ||
      String(message.from?.id) !== ownerId || message.from?.is_bot ||
      typeof message.text !== 'string') return null
  const command = message.text.trim().split(/\s+/)[0].split('@')[0].toLowerCase()
  return { updateId: update.update_id, command: command === '/stats' ? 'stats' : 'help' }
}
