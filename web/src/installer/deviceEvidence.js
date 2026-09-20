// Decode only known crash signatures and numeric firmware checkpoints. Never
// retain console text: it can contain network names, credentials or locations.
const PANIC_REASONS = new Set([
  'LoadProhibited', 'StoreProhibited', 'InstrFetchProhibited', 'IllegalInstruction',
  'LoadStoreError', 'LoadStoreAlignment', 'IntegerDivideByZero',
  'Interrupt wdt timeout on CPU0', 'Interrupt wdt timeout on CPU1',
  'Unhandled debug exception', 'Cache disabled but cached memory region accessed',
])
const KINDS = new Set(['panic', 'backtrace', 'pc', 'reset', 'elf', 'checkpoint', 'stack-overflow', 'brownout', 'task-watchdog', 'abort'])
const codeAddress = (value) => Number.isInteger(value) && value >= 0x40000000 && value <= 0x43ffffff

export function sanitizeDeviceEvidence(input) {
  if (!input || !KINDS.has(input.kind)) return undefined
  const result = { kind: input.kind }
  if (PANIC_REASONS.has(input.reason)) result.reason = input.reason
  for (const field of ['offsetMs', 'stage', 'heap', 'min', 'stack', 'reset', 'uptimeMs']) {
    const value = input[field]
    if (Number.isSafeInteger(value) && value >= 0 && value <= 0xffffffff) result[field] = value
  }
  if (Array.isArray(input.addresses)) result.addresses = input.addresses.filter(codeAddress).slice(0, 24)
  if (typeof input.elf === 'string' && /^[a-f0-9]{8,64}$/i.test(input.elf)) result.elf = input.elf.toLowerCase()
  return result
}

export function filterDeviceEvidence(input) {
  return Array.isArray(input) ? input.slice(-24).map(sanitizeDeviceEvidence).filter(Boolean) : []
}

export function createDeviceConsoleDecoder(record) {
  let line = ''
  let overflow = false
  function emit(value) { record(sanitizeDeviceEvidence(value)) }
  function consume() {
    const text = line.replace(/\x1b\[[0-9;]*m/g, '').trim()
    let match
    if ((match = text.match(/^Guru Meditation Error: Core\s+\d+ panic'ed \(([^)]+)\)/))) {
      emit({ kind: 'panic', reason: match[1] })
    } else if ((match = text.match(/^Backtrace:\s+((?:0x[0-9a-f]{8}:0x[0-9a-f]{8}\s*)+)/i))) {
      emit({ kind: 'backtrace', addresses: [...match[1].matchAll(/(0x[0-9a-f]{8}):0x[0-9a-f]{8}/gi)].map((item) => parseInt(item[1], 16)) })
    } else if ((match = text.match(/^PC\s*:\s*(0x[0-9a-f]{8})\b/i))) {
      emit({ kind: 'pc', addresses: [parseInt(match[1], 16)] })
    } else if ((match = text.match(/^rst:0x([0-9a-f]+)\s*\(/i))) {
      emit({ kind: 'reset', reset: parseInt(match[1], 16) })
    } else if ((match = text.match(/^ELF file SHA256:\s*([0-9a-f]{8,64})\s*$/i))) {
      emit({ kind: 'elf', elf: match[1] })
    } else if ((match = text.match(/^WINDDIAG stage=(\d+) heap=(\d+) min=(\d+) stack=(\d+) reset=(\d+)(?: uptime=(\d+))?$/))) {
      emit({ kind: 'checkpoint', ...Object.fromEntries(['stage', 'heap', 'min', 'stack', 'reset'].map((key, i) => [key, Number(match[i + 1])])), ...(match[6] ? { uptimeMs: Number(match[6]) } : {}) })
    } else if (/^\*\*\*ERROR\*\*\* A stack overflow in task /.test(text)) {
      emit({ kind: 'stack-overflow' })
    } else if (/^Brownout detector was triggered/.test(text)) {
      emit({ kind: 'brownout' })
    } else if (/^E \(\d+\) task_wdt: Task watchdog got triggered/.test(text)) {
      emit({ kind: 'task-watchdog' })
    } else if ((match = text.match(/^abort\(\) was called at PC (0x[0-9a-f]{8}) on core \d+/i))) {
      emit({ kind: 'abort', addresses: [parseInt(match[1], 16)] })
    }
  }
  return {
    feed(bytes) {
      for (const byte of bytes) {
        if (byte === 10 || byte === 13) {
          if (!overflow) consume()
          line = ''; overflow = false
        } else if (line.length < 1024) {
          line += String.fromCharCode(byte)
        } else overflow = true
      }
    },
    // Called at binary frame boundaries and on close. A pending final line
    // may be a panic without a trailing newline; never retain raw fragments.
    flush() { if (!overflow) consume(); line = ''; overflow = false },
  }
}
