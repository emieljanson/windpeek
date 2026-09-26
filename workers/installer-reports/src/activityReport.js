export const DAY_MS = 86400000
export const WEEK_MS = 7 * DAY_MS
const date = value => new Intl.DateTimeFormat('nl-NL', { day: 'numeric', month: 'short', timeZone: 'Europe/Amsterdam' }).format(value)

export function activityReport({ current, previous, total, startedAt, now }) {
  const comparisonReady = startedAt <= now - 2 * WEEK_MS
  const metric = (label, key) => {
    const count = current[key] || 0
    if (!comparisonReady) return `${label}: ${count}`
    const change = count - (previous[key] || 0)
    return `${label}: ${count} (${change > 0 ? '+' : ''}${change} t.o.v. vorige week)`
  }
  return [
    '🌬️ Windpeek · weekoverzicht',
    `${date(now - WEEK_MS)} – ${date(now)}`, '',
    metric('👀 Websitebezoeken', 'visit'),
    metric('🎉 Installaties', 'install'),
    metric('🔄 Herinstallaties', 'reinstall'),
    metric('⬆️ Firmware-updates', 'update-firmware'),
    metric('🛠 Foutrapporten', 'failure'),
    '',
    `Sinds de start: ${total} installaties.`,
    `Meting sinds ${date(startedAt)}. Bezoeken zijn sessies per tabblad; installaties zijn geen unieke apparaten.`,
    ...(!comparisonReady ? ['Vergelijking volgt zodra er twee volledige weken zijn gemeten.'] : []),
  ].join('\n')
}
