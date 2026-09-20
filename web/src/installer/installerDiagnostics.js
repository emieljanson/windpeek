import { sanitizeDeviceEvidence } from './deviceEvidence'

const DEFAULT_MAX_ENTRIES = 100
const DEFAULT_MAX_TEXT_BYTES = 50 * 1024
const DEFAULT_MAX_TEXT_ENTRY_CHARS = 512
const REDACTED = '[redacted]'

const CONTEXT_FIELDS = new Set([
  'phase', 'errorCode', 'action', 'route', 'release', 'boardId', 'chipFamily',
  'layoutVersion', 'browser', 'os', 'attempt', 'selectedBoardId',
  'detectedBoardId', 'detectedFirmwareVersion', 'releaseBoardId',
  'releaseVersion', 'connectionKind', 'decisionReason',
])
const MEASUREMENT_FIELDS = new Set([
  'elapsedMs', 'durationMs', 'fileIndex', 'writtenBytes', 'totalBytes',
  'retryCount', 'entryCount', 'textBytes', 'baudRate',
  'requestTimeoutMs', 'receivedBytes', 'receivedChunks', 'discardedBytes',
  'staleFrames', 'bufferedBytes', 'expectedFrameBytes',
])

// Deliberately exclude configuration, network names and arbitrary device text.
export function sanitizeDeviceState(input) {
  if (!input || typeof input !== 'object') return undefined
  const state = {}
  for (const [field, allowed] of Object.entries({
    wifi: ['connected', 'disconnected'],
    render: ['valid', 'pending'],
    apply: ['idle', 'applying', 'complete', 'render_failed', 'commit_failed'],
  })) {
    if (allowed.includes(input[field])) state[field] = input[field]
  }
  if (typeof input.wifiConfigured === 'boolean') state.wifiConfigured = input.wifiConfigured
  if (Number.isSafeInteger(input.applyError)) state.applyError = input.applyError
  for (const field of ['deviceStage', 'freeHeap', 'minimumHeap', 'taskStackFree', 'resetReason', 'uptimeMs']) {
    if (Number.isSafeInteger(input[field]) && input[field] >= 0 && input[field] <= 0xffffffff) state[field] = input[field]
  }
  return Object.keys(state).length ? state : undefined
}

function byteLength(value) {
  return new TextEncoder().encode(value).byteLength
}

function safeScalar(value) {
  if (typeof value === 'string') return value
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (typeof value === 'boolean') return value
  return undefined
}

function collectSensitiveValues(input, output, seen) {
  if (typeof input === 'string') {
    if (input) output.add(input)
    return
  }
  if (!input || typeof input !== 'object' || seen.has(input)) return
  if (ArrayBuffer.isView(input) || input instanceof ArrayBuffer) return
  seen.add(input)
  let keys
  try { keys = Object.keys(input) } catch { return }
  for (const key of keys) {
    try { collectSensitiveValues(input[key], output, seen) } catch {}
  }
}

function replaceSensitiveValues(value, sensitiveValues) {
  let result = value
  const ordered = [...sensitiveValues].sort((a, b) => b.length - a.length)
  for (const sensitive of ordered) {
    if (!sensitive) continue
    result = result.split(sensitive).join(REDACTED)
  }
  return result
}

export function sanitizeDiagnosticText(input, sensitiveValues = []) {
  let value
  try { value = String(input ?? '') } catch { return '' }
  value = replaceSensitiveValues(value, sensitiveValues)
  return value
    .replace(/\b(authorization|proxy-authorization)\s*[:=]\s*(?:bearer\s+)?[^\s,;]+/gi, (_, key) => `${key}=${REDACTED}`)
    .replace(/\b(?:password|passphrase|passwd|pwd|ssid|bssid|token|api[_-]?key|secret)\s*[:=]\s*(?:"[^"]*"|'[^']*'|[^\s,;]+)/gi, (match) => `${match.split(/[:=]/, 1)[0]}=${REDACTED}`)
    .replace(/\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b/gi, REDACTED)
    .replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g, REDACTED)
    .replace(/\b(?:[A-F0-9]{2}:){5}[A-F0-9]{2}\b/gi, REDACTED)
    .replace(/(?<![0-9A-F:])(?=[0-9A-F:]*:[0-9A-F:]*:)(?:[0-9A-F]{0,4}:){2,7}[0-9A-F]{0,4}(?![0-9A-F:])/gi, REDACTED)
    .replace(/\b(?:latitude|longitude|lat|lon|lng)\s*[:=]\s*-?\d+(?:\.\d+)?/gi, (match) => `${match.split(/[:=]/, 1)[0]}=${REDACTED}`)
    .replace(/(https?:\/\/[^\s?]+)\?[^\s]*/gi, '$1?[redacted]')
}

function truncate(value, maxChars) {
  return value.length > maxChars ? value.slice(0, maxChars) : value
}

function sanitizeKey(value, sensitiveValues, maxChars = 80) {
  return truncate(sanitizeDiagnosticText(value, sensitiveValues), maxChars)
    .replace(/[^a-zA-Z0-9_.:/-]/g, '_')
}

export function createInstallerDiagnostics({
  now = () => Date.now(),
  maxEntries = DEFAULT_MAX_ENTRIES,
  maxTextBytes = DEFAULT_MAX_TEXT_BYTES,
  maxTextEntryChars = DEFAULT_MAX_TEXT_ENTRY_CHARS,
} = {}) {
  const startedAt = now()
  const sensitiveValues = new Set()
  const context = {}
  const entries = []
  const deviceEvidence = []
  let textBytes = 0
  let credentialLocks = 0
  let destroyed = false

  function evictToBounds() {
    while (entries.length > maxEntries || textBytes > maxTextBytes) {
      const removed = entries.shift()
      textBytes -= removed?._textBytes ?? 0
    }
  }

  function registerSensitiveValues(input) {
    if (destroyed) return
    collectSensitiveValues(input, sensitiveValues, new WeakSet())
  }

  function acquireCredentialLock(values) {
    if (destroyed) return () => {}
    registerSensitiveValues(values)
    credentialLocks += 1
    let released = false
    return () => {
      if (released) return
      released = true
      credentialLocks = Math.max(0, credentialLocks - 1)
    }
  }

  function setContext(patch = {}) {
    if (destroyed || !patch || typeof patch !== 'object') return
    for (const field of CONTEXT_FIELDS) {
      let candidate
      try { candidate = safeScalar(patch[field]) } catch { continue }
      if (candidate === undefined) continue
      context[field] = typeof candidate === 'string'
        ? truncate(sanitizeDiagnosticText(candidate, sensitiveValues), 160)
        : candidate
    }
  }

  function record(candidate = {}) {
    if (destroyed || !candidate || typeof candidate !== 'object') return
    let category
    let operation
    try {
      category = sanitizeKey(candidate.category ?? 'installer', sensitiveValues)
      operation = sanitizeKey(candidate.operation ?? 'event', sensitiveValues)
    } catch { return }
    const entry = {
      offsetMs: Math.max(0, now() - startedAt),
      category,
      operation,
    }
    let status
    let message
    try {
      status = safeScalar(candidate.status)
      message = safeScalar(candidate.message)
    } catch {}
    if (status !== undefined) entry.status = sanitizeKey(status, sensitiveValues)
    const deviceState = sanitizeDeviceState(candidate.deviceState)
    if (deviceState) entry.deviceState = deviceState
    // Credentials may be split across multiple serial messages, which makes
    // exact-value redaction insufficient. Keep the structured event, but no
    // free-form text, for the short period credentials are in memory.
    if (message !== undefined && credentialLocks === 0) {
      entry.message = truncate(sanitizeDiagnosticText(message, sensitiveValues), maxTextEntryChars)
    }

    if (candidate.measurements && typeof candidate.measurements === 'object') {
      const measurements = {}
      for (const field of MEASUREMENT_FIELDS) {
        let value
        try { value = candidate.measurements[field] } catch { continue }
        if (typeof value === 'number' && Number.isFinite(value)) measurements[field] = value
      }
      if (Object.keys(measurements).length) entry.measurements = measurements
    }

    entry._textBytes = byteLength(entry.message ?? '')
    entries.push(entry)
    textBytes += entry._textBytes
    evictToBounds()
  }

  function recordDeviceEvidence(candidate) {
    if (destroyed) return
    const evidence = sanitizeDeviceEvidence({ ...candidate, offsetMs: Math.max(0, now() - startedAt) })
    if (!evidence) return
    // Keep the latest checkpoint without letting polling evict a crash.
    if (evidence.kind === 'checkpoint') {
      const previous = deviceEvidence.findIndex((item) => item.kind === 'checkpoint')
      if (previous !== -1) deviceEvidence.splice(previous, 1)
    }
    deviceEvidence.push(evidence)
    if (deviceEvidence.length > 24) deviceEvidence.shift()
  }

  function snapshot() {
    if (credentialLocks > 0) return null
    if (destroyed) return { context: {}, entries: [], textBytes: 0 }
    const safeContext = {}
    for (const [key, value] of Object.entries(context)) {
      safeContext[key] = typeof value === 'string'
        ? sanitizeDiagnosticText(value, sensitiveValues)
        : value
    }
    const safeEntries = entries.map(({ _textBytes, ...entry }) => {
      const safeEntry = { ...entry }
      if (safeEntry.message) safeEntry.message = sanitizeDiagnosticText(safeEntry.message, sensitiveValues)
      return safeEntry
    })
    return { context: safeContext, entries: safeEntries, textBytes, ...(deviceEvidence.length ? { deviceEvidence: deviceEvidence.map((item) => ({ ...item })) } : {}) }
  }

  function destroy() {
    destroyed = true
    entries.splice(0)
    deviceEvidence.splice(0)
    for (const key of Object.keys(context)) delete context[key]
    sensitiveValues.clear()
    textBytes = 0
    credentialLocks = 0
  }

  return {
    registerSensitiveValues,
    recordDeviceEvidence,
    acquireCredentialLock,
    setContext,
    record,
    snapshot,
    destroy,
    get credentialsLocked() { return credentialLocks > 0 },
  }
}
