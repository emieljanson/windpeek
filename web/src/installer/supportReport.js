// Only explicitly named settings are reportable. Never copy a device response,
// configuration or history entry with a spread: they may contain credentials.
const text = (value, limit = 64) => typeof value === 'string' ? value.replace(/[\u0000-\u001f]/g, '').slice(0, limit) : undefined
const number = (value) => typeof value === 'number' && Number.isFinite(value) ? value : undefined
function pick(input, strings = [], numbers = [], booleans = []) {
  const out = {}
  if (!input || typeof input !== 'object') return out
  for (const key of strings) if (text(input[key]) !== undefined) out[key] = text(input[key])
  for (const key of numbers) if (number(input[key]) !== undefined) out[key] = number(input[key])
  for (const key of booleans) if (typeof input[key] === 'boolean') out[key] = input[key]
  return out
}

export function sanitizeSupportConfiguration(input, nested = false) {
  if (!input || typeof input !== 'object') return undefined
  const out = pick(input, ['boardId', 'deviceTimezone', 'forecastModel', 'digest'], ['version', 'generation'])
  out.spot = pick(input.spot, ['id', 'name', 'timezone'], ['latitude', 'longitude'])
  out.display = pick(input.display, ['windSize', 'swellSize', 'swellModel', 'timeFormat', 'temperatureUnit'],
    ['threshold'], ['showThreshold', 'showWeather', 'showTemperature', 'showTide', 'showDedicatedFooter'])
  if (Array.isArray(input.display?.moduleOrder)) out.display.moduleOrder = input.display.moduleOrder
    .slice(0, 5).filter(value => ['wind', 'swell', 'weather', 'temperature', 'tide'].includes(value))
  if (!nested && Array.isArray(input.additionalSpots)) out.additionalSpots = input.additionalSpots
    .slice(0, 9).map(value => sanitizeSupportConfiguration(value, true)).filter(Boolean)
  return out
}

export function sanitizeSupportReport(input, nested = false) {
  if (!input || typeof input !== 'object') return undefined
  const out = pick(input, ['historyStatus', 'firmwareVersion', 'hardwareModel'],
    ['capturedAt', 'startedAt', 'oldestSequence', 'newestSequence', 'storageErrors'], ['configurationInstalled'])
  if (/^[a-f0-9]{32}$/.test(input.deviceId)) out.deviceId = input.deviceId
  if (!nested && input.beforeFirmwareErase) out.beforeFirmwareErase = sanitizeSupportReport(input.beforeFirmwareErase, true)
  for (const key of ['selectedConfiguration', 'installedConfiguration']) {
    const configuration = sanitizeSupportConfiguration(input[key])
    if (configuration) out[key] = configuration
  }
  out.history = (Array.isArray(input.history) ? input.history : []).slice(-32).map(entry => {
    const safe = pick(entry, ['firmwareVersion'], ['sequence', 'timestamp', 'uptimeMs', 'result', 'stage',
      'resetReason', 'wakeReasons', 'batteryPercent', 'httpStatus', 'transportError', 'parseError', 'fetchResult',
      'panelPhase', 'panelWaitMs'], ['usbPowered', 'wifiConnected', 'attemptedFetch'])
    if (['boot', 'setup-started', 'setup-complete', 'setup-failed', 'refresh-complete', 'refresh-failed'].includes(entry?.kind)) safe.kind = entry.kind
    const configuration = sanitizeSupportConfiguration(entry?.configuration)
    if (configuration) safe.configuration = configuration
    return safe
  })
  return out
}

export async function readSupportReport(protocol, diagnostics, isCurrent = () => true) {
  let requestFailed = false
  const request = async (sequence) => {
    try { return await protocol.request('get_diagnostics', { sequence }, 5000) }
    catch (error) { requestFailed = true; throw error }
  }
  try {
    const metadata = await request(0)
    if (!isCurrent()) return
    if (metadata.status !== 'ok') throw new Error('Diagnostics unavailable')
    const newest = metadata.newestSequence
    const oldest = metadata.oldestSequence
    if (!Number.isSafeInteger(newest) || !Number.isSafeInteger(oldest) ||
        oldest < 0 || newest > 0xffffffff || newest < oldest || newest - oldest > 31 ||
        (oldest === 0 && newest !== 0)) throw new Error('Invalid diagnostic range')
    const history = []
    // Freeze the range from the first response. Concurrent refreshes cannot
    // make this read unbounded; overwritten entries are explicitly partial.
    diagnostics.setSupport?.({ deviceId: metadata.deviceId, firmwareVersion: metadata.firmwareVersion,
      oldestSequence: oldest, newestSequence: newest, storageErrors: metadata.storageErrors,
      configurationInstalled: metadata.configurationInstalled, installedConfiguration: metadata.configuration,
      history, historyStatus: 'reading' })
    let partial = false
    for (let sequence = oldest; sequence > 0 && sequence <= newest; sequence += 1) {
      const response = await request(sequence)
      if (!isCurrent()) return
      if (response.status === 'ok' && response.entry?.sequence === sequence) history.push(response.entry)
      else partial = true
      diagnostics.setSupport?.({ history, historyStatus: 'reading' })
    }
    diagnostics.setSupport?.({ history, historyStatus: partial ? 'partial' : 'complete' })
  } catch {
    if (isCurrent()) diagnostics.setSupport?.({ historyStatus: 'unavailable' })
  }
  return { requestFailed }
}
