import { sanitizeDiagnosticText } from './installerDiagnostics'

const REFERENCE_ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ'
const REFERENCE_PATTERN = /^WS-[0-9A-HJKMNP-TV-Z]{10}$/
const INSTALLER_MARKER = 'installer'
const MAX_REPORTED_OCCURRENCES = 200

const TAG_FIELDS = new Set([
  'windpeek.diagnostic', 'windpeek.reference', 'error_code', 'phase', 'action',
  'route', 'release', 'board_id', 'chip_family', 'layout_version', 'browser', 'os',
  'build_release', 'selected_board_id', 'detected_board_id', 'detected_firmware',
  'release_board_id', 'release_version', 'connection_kind', 'decision_reason',
])
const CONTEXT_FIELDS = new Set([
  'phase', 'errorCode', 'action', 'route', 'release', 'boardId', 'chipFamily',
  'layoutVersion', 'browser', 'os', 'attempt', 'selectedBoardId',
  'detectedBoardId', 'detectedFirmwareVersion', 'releaseBoardId',
  'releaseVersion', 'connectionKind', 'decisionReason',
])
const ENTRY_FIELDS = new Set(['offsetMs', 'category', 'operation', 'status', 'message', 'measurements'])
const MEASUREMENT_FIELDS = new Set([
  'elapsedMs', 'durationMs', 'fileIndex', 'writtenBytes', 'totalBytes',
  'retryCount', 'entryCount', 'textBytes', 'baudRate',
])

function safeString(value, maxLength = 240) {
  if (typeof value !== 'string' && typeof value !== 'number' && typeof value !== 'boolean') return undefined
  return sanitizeDiagnosticText(String(value)).slice(0, maxLength)
}

function pickScalars(input, fields, maxLength) {
  const output = {}
  if (!input || typeof input !== 'object') return output
  for (const field of fields) {
    let value
    try { value = input[field] } catch { continue }
    if (typeof value === 'number' && Number.isFinite(value)) output[field] = value
    else {
      const safe = safeString(value, maxLength)
      if (safe !== undefined) output[field] = safe
    }
  }
  return output
}

function filterTimeline(input) {
  if (!Array.isArray(input)) return []
  return input.slice(-100).map((candidate) => {
    const entry = pickScalars(candidate, ENTRY_FIELDS, 512)
    if (candidate?.measurements && typeof candidate.measurements === 'object') {
      const measurements = {}
      for (const field of MEASUREMENT_FIELDS) {
        const value = candidate.measurements[field]
        if (typeof value === 'number' && Number.isFinite(value)) measurements[field] = value
      }
      if (Object.keys(measurements).length) entry.measurements = measurements
    }
    return entry
  })
}

function filterException(input) {
  if (!Array.isArray(input?.values)) return undefined
  const values = input.values.slice(-4).map((candidate) => {
    const value = {}
    const type = safeString(candidate?.type, 120)
    const message = safeString(candidate?.value, 512)
    if (type) value.type = type
    if (message) value.value = message
    if (Array.isArray(candidate?.stacktrace?.frames)) {
      const frames = candidate.stacktrace.frames.slice(-80).map((frame) => {
        const safeFrame = {}
        const filename = safeString(frame?.filename, 500)
        const functionName = safeString(frame?.function, 160)
        const moduleName = safeString(frame?.module, 160)
        if (filename) safeFrame.filename = filename
        if (functionName) safeFrame.function = functionName
        if (moduleName) safeFrame.module = moduleName
        if (Number.isFinite(frame?.lineno)) safeFrame.lineno = frame.lineno
        if (Number.isFinite(frame?.colno)) safeFrame.colno = frame.colno
        if (typeof frame?.in_app === 'boolean') safeFrame.in_app = frame.in_app
        return safeFrame
      })
      value.stacktrace = { frames }
    }
    return value
  })
  return values.length ? { values } : undefined
}

export function filterInstallerEvent(event) {
  if (event?.tags?.['windpeek.diagnostic'] !== INSTALLER_MARKER) return null
  const filtered = {
    event_id: event.event_id,
    timestamp: event.timestamp,
    platform: 'javascript',
    level: 'error',
    tags: pickScalars(event.tags, TAG_FIELDS, 160),
    contexts: { installer: pickScalars(event.contexts?.installer, CONTEXT_FIELDS, 240) },
    extra: {
      timeline: filterTimeline(event.extra?.timeline),
      textBytes: Number.isFinite(event.extra?.textBytes) ? event.extra.textBytes : 0,
    },
  }
  const release = safeString(event.release, 160)
  const environment = safeString(event.environment, 80)
  const exception = filterException(event.exception)
  if (release) filtered.release = release
  if (environment) filtered.environment = environment
  if (exception) filtered.exception = exception
  return filtered
}

function createReference(randomBytes) {
  const bytes = randomBytes()
  if (!(bytes instanceof Uint8Array) || bytes.byteLength < 7) throw new Error('Secure random bytes unavailable')
  let buffer = 0
  let bits = 0
  let output = ''
  for (const byte of bytes) {
    buffer = (buffer << 8) | byte
    bits += 8
    while (bits >= 5 && output.length < 10) {
      output += REFERENCE_ALPHABET[(buffer >>> (bits - 5)) & 31]
      bits -= 5
    }
    if (output.length === 10) break
    buffer &= (1 << bits) - 1
  }
  if (output.length !== 10) throw new Error('Insufficient random bytes')
  return `WS-${output}`
}

export function isInstallerDiagnosticReference(value) {
  return typeof value === 'string' && REFERENCE_PATTERN.test(value)
}

function defaultRandomBytes() {
  const bytes = new Uint8Array(7)
  globalThis.crypto.getRandomValues(bytes)
  return bytes
}

function browserContext(navigatorApi = globalThis.navigator) {
  const userAgent = String(navigatorApi?.userAgent ?? '')
  const candidates = [
    ['Edge', /Edg\/(\d+)/],
    ['Chrome', /(?:Chrome|CriOS)\/(\d+)/],
    ['Firefox', /(?:Firefox|FxiOS)\/(\d+)/],
    ['Safari', /Version\/(\d+).+Safari/],
  ]
  const [browser, major] = candidates
    .map(([name, pattern]) => [name, userAgent.match(pattern)?.[1]])
    .find(([, version]) => version) ?? ['Other', undefined]
  const os = /Windows/i.test(userAgent) ? 'Windows'
    : /Android/i.test(userAgent) ? 'Android'
      : /iPhone|iPad|iPod/i.test(userAgent) ? 'iOS'
        : /Mac OS X/i.test(userAgent) ? 'macOS'
          : /Linux/i.test(userAgent) ? 'Linux' : 'Other'
  return { browser: major ? `${browser} ${major}` : browser, os }
}

function safeError(error) {
  const message = safeString(error?.message, 512) ?? 'Installer failure'
  const output = new Error(message)
  output.name = safeString(error?.name, 120) ?? 'InstallerError'
  if (typeof error?.stack === 'string') output.stack = sanitizeDiagnosticText(error.stack).slice(0, 24_000)
  return output
}

function createDeliveryTracker() {
  const pending = new Map()
  const completed = new Map()
  function settle(eventId, result) {
    if (!eventId) return
    const waiter = pending.get(eventId)
    if (waiter) {
      pending.delete(eventId)
      waiter(result)
    } else {
      completed.set(eventId, result)
      if (completed.size > MAX_REPORTED_OCCURRENCES) completed.delete(completed.keys().next().value)
    }
  }
  function wait(eventId, timeoutMs) {
    if (completed.has(eventId)) {
      const result = completed.get(eventId)
      completed.delete(eventId)
      return Promise.resolve(result)
    }
    return new Promise((resolve) => {
      const timeout = setTimeout(() => {
        pending.delete(eventId)
        resolve({ accepted: false })
      }, timeoutMs)
      pending.set(eventId, (result) => {
        clearTimeout(timeout)
        resolve(result)
      })
    })
  }
  return { settle, wait }
}

export function createSentryReporter({
  enabled = Boolean(import.meta.env.PROD),
  dsn = import.meta.env.VITE_SENTRY_DSN ?? '',
  release = import.meta.env.VITE_SENTRY_RELEASE ?? '',
  environment = 'production',
  timeoutMs = 5000,
  navigatorApi = globalThis.navigator,
  randomBytes = defaultRandomBytes,
  loadSentry = () => import('@sentry/browser'),
} = {}) {
  const tracker = createDeliveryTracker()
  const reports = new Map()
  let sdkPromise

  async function initialize() {
    if (!sdkPromise) sdkPromise = loadSentry().then((sdk) => {
      sdk.init({
        dsn,
        release: release || undefined,
        environment,
        defaultIntegrations: false,
        sendClientReports: false,
        enableLogs: false,
        enableMetrics: false,
        tracesSampleRate: 0,
        tracePropagationTargets: [],
        dataCollection: {
          userInfo: false,
          cookies: false,
          httpHeaders: { request: false, response: false },
          httpBodies: [],
          urlQueryParams: false,
          graphQL: { document: false, variables: false },
          genAI: { inputs: false, outputs: false },
          databaseQueryData: false,
          stackFrameVariables: false,
          frameContextLines: 0,
        },
        beforeSend: filterInstallerEvent,
        transport: (options) => {
          const transport = sdk.makeFetchTransport(options)
          return {
            send(envelope) {
              const eventId = envelope?.[0]?.event_id
              return Promise.resolve(transport.send(envelope)).then(
                (response) => {
                  tracker.settle(eventId, { accepted: response?.statusCode >= 200 && response.statusCode < 300 })
                  return response
                },
                (error) => {
                  tracker.settle(eventId, { accepted: false })
                  throw error
                },
              )
            },
            flush: (transportTimeout) => transport.flush(transportTimeout),
          }
        },
      })
      return sdk
    })
    return sdkPromise
  }

  async function send(input) {
    if (!enabled || !dsn) return { status: 'failed' }
    let reference
    let sdk
    try {
      reference = createReference(randomBytes)
      sdk = await initialize()
    } catch { return { status: 'failed' } }

    const environmentContext = browserContext(navigatorApi)
    const context = { ...pickScalars(input.snapshot?.context, CONTEXT_FIELDS, 240), ...environmentContext }
    const tags = {
      'windpeek.diagnostic': INSTALLER_MARKER,
      'windpeek.reference': reference,
      error_code: context.errorCode ?? safeString(input.error?.code, 120),
      phase: context.phase ?? safeString(input.phase, 120),
      action: context.action,
      route: context.route,
      release: context.release,
      board_id: context.boardId,
      chip_family: context.chipFamily,
      layout_version: context.layoutVersion,
      browser: context.browser,
      os: context.os,
      build_release: release || undefined,
      selected_board_id: context.selectedBoardId,
      detected_board_id: context.detectedBoardId,
      detected_firmware: context.detectedFirmwareVersion,
      release_board_id: context.releaseBoardId,
      release_version: context.releaseVersion,
      connection_kind: context.connectionKind,
      decision_reason: context.decisionReason,
    }
    for (const key of Object.keys(tags)) if (tags[key] === undefined) delete tags[key]

    let eventId
    try {
      eventId = sdk.captureException(safeError(input.error), {
        tags,
        contexts: { installer: context },
        extra: {
          timeline: filterTimeline(input.snapshot?.entries),
          textBytes: Number.isFinite(input.snapshot?.textBytes) ? input.snapshot.textBytes : 0,
        },
      })
      const delivery = tracker.wait(eventId, timeoutMs)
      void Promise.resolve(sdk.flush(timeoutMs)).catch(() => {})
      const result = await delivery
      return result.accepted ? { status: 'sent', reference } : { status: 'failed' }
    } catch {
      return { status: 'failed' }
    }
  }

  return {
    report(input) {
      const key = String(input?.occurrence ?? '')
      if (!key) return Promise.resolve({ status: 'failed' })
      if (reports.has(key)) return reports.get(key)
      const result = send(input)
      reports.set(key, result)
      if (reports.size > MAX_REPORTED_OCCURRENCES) reports.delete(reports.keys().next().value)
      return result
    },
  }
}

export const installerSentryReporter = createSentryReporter()
