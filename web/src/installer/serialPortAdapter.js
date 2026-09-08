import { InstallerError, INSTALLER_ERROR_CODES, isChooserCancellation } from './installerErrors'

const MAGIC = new TextEncoder().encode('WINDSC01')
const HEADER_SIZE = 24
const MAX_PAYLOAD_SIZE = 4096

export function getSerialSupport({
  navigatorApi = globalThis.navigator,
  locationApi = globalThis.location,
  secureContext = globalThis.isSecureContext,
} = {}) {
  const mobile = navigatorApi?.userAgentData?.mobile === true || /Android|iPhone|iPad|iPod/i.test(navigatorApi?.userAgent ?? '')
  const secure = typeof secureContext === 'boolean'
    ? secureContext
    : locationApi?.protocol === 'https:' || ['localhost', '127.0.0.1'].includes(locationApi?.hostname)
  if (mobile) return { supported: false, reason: 'desktop-required' }
  if (!secure) return { supported: false, reason: 'secure-context-required' }
  if (!navigatorApi?.serial) return { supported: false, reason: 'browser-not-supported' }
  return { supported: true, reason: null }
}

export async function requestInstallerPort(navigatorApi = globalThis.navigator) {
  const support = getSerialSupport({ navigatorApi })
  if (!support.supported) {
    throw new InstallerError(INSTALLER_ERROR_CODES.UNSUPPORTED, 'Open Windpeek in a current desktop version of Firefox, Chrome, or Edge.', { recoverable: false })
  }
  try {
    return await navigatorApi.serial.requestPort()
  } catch (error) {
    if (isChooserCancellation(error)) return null
    throw new InstallerError(INSTALLER_ERROR_CODES.DEVICE_NOT_ALLOWED, 'Windpeek could not access the selected USB device.', { cause: error })
  }
}

export async function findGrantedInstallerPort({ navigatorApi = globalThis.navigator, classify, signal } = {}) {
  if (!navigatorApi?.serial) return null
  for (const port of await navigatorApi.serial.getPorts()) {
    if (signal?.aborted) return null
    if (await classify(port)) return port
  }
  return null
}

function crc32(bytes) {
  let crc = 0xffffffff
  for (const byte of bytes) {
    crc ^= byte
    for (let bit = 0; bit < 8; bit += 1) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1))
  }
  return (crc ^ 0xffffffff) >>> 0
}

function magicOffset(bytes, length) {
  for (let offset = 0; offset <= length - MAGIC.length; offset += 1) {
    if (MAGIC.every((byte, index) => bytes[offset + index] === byte)) return offset
  }
  return -1
}

export function encodeProtocolFrame({ requestId, messageType = 1, payload }) {
  const body = new TextEncoder().encode(JSON.stringify(payload))
  if (body.length > MAX_PAYLOAD_SIZE) throw new RangeError('Protocol payload is too large')
  const frame = new Uint8Array(HEADER_SIZE + body.length)
  frame.set(MAGIC)
  const view = new DataView(frame.buffer)
  view.setUint16(8, 1, true)
  view.setUint16(10, messageType, true)
  view.setUint32(12, requestId, true)
  view.setUint32(16, body.length, true)
  view.setUint32(20, crc32(body), true)
  frame.set(body, HEADER_SIZE)
  return frame
}

export function decodeProtocolFrame(frame) {
  const bytes = frame instanceof Uint8Array ? frame : new Uint8Array(frame)
  if (bytes.length < HEADER_SIZE || !MAGIC.every((byte, index) => bytes[index] === byte)) throw new Error('Invalid protocol frame')
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const length = view.getUint32(16, true)
  const body = bytes.slice(HEADER_SIZE)
  if (view.getUint16(8, true) !== 1 || length > MAX_PAYLOAD_SIZE || body.length !== length || view.getUint32(20, true) !== crc32(body)) {
    throw new Error('Invalid protocol frame')
  }
  return { messageType: view.getUint16(10, true), requestId: view.getUint32(12, true), payload: JSON.parse(new TextDecoder().decode(body)) }
}

export function createSerialProtocol(port, {
  baudRate = 115200,
  timeoutMs = 15000,
  diagnostics,
  waitFor = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)),
} = {}) {
  let requestId = 0
  let reader
  let writer
  let buffered = new Uint8Array(0)
  let requestQueue = Promise.resolve()

  function record(entry) {
    try { diagnostics?.record?.(entry) } catch {}
  }

  function append(chunk) {
    const combined = new Uint8Array(buffered.length + chunk.length)
    combined.set(buffered)
    combined.set(chunk, buffered.length)
    buffered = combined
  }

  function takeResponse(requestId) {
    while (true) {
      const offset = magicOffset(buffered, buffered.length)
      if (offset < 0) {
        const retainedOffset = Math.max(0, buffered.length - (MAGIC.length - 1))
        buffered = buffered.slice(retainedOffset)
        return null
      }
      if (offset > 0) {
        buffered = buffered.slice(offset)
      }
      if (buffered.length < HEADER_SIZE) return null
      const declared = new DataView(buffered.buffer, buffered.byteOffset).getUint32(16, true)
      if (declared > MAX_PAYLOAD_SIZE) {
        throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'The device returned an invalid response.')
      }
      const frameSize = HEADER_SIZE + declared
      if (buffered.length < frameSize) return null
      const response = decodeProtocolFrame(buffered.slice(0, frameSize))
      buffered = buffered.slice(frameSize)
      // A delayed response from an older request must not poison the next
      // exchange. Consume it and continue looking in the retained byte stream.
      if (response.requestId !== requestId) continue
      if (response.messageType !== 2 || response.payload?.error) {
        throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'The device rejected the request.')
      }
      return response.payload
    }
  }

  async function performRequest(command, values, requestedTimeout) {
    const id = ++requestId
    const startedAt = Date.now()
    record({ category: 'protocol', operation: command, status: 'started' })
    await writer.write(encodeProtocolFrame({ requestId: id, payload: { command, ...values } }))
    let timeoutId
    const timeout = new Promise((_, reject) => {
      timeoutId = setTimeout(() => {
        void reader.cancel().catch(() => {})
        reject(new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'The device stopped responding.'))
      }, requestedTimeout)
    })
    const read = (async () => {
      while (true) {
        const response = takeResponse(id)
        if (response) return response
        const result = await reader.read()
        if (result.done) throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'The USB connection was lost.')
        append(result.value)
      }
    })()
    try {
      const response = await Promise.race([read, timeout])
      record({
        category: 'protocol', operation: command, status: response?.status ?? 'ok',
        measurements: { durationMs: Date.now() - startedAt },
      })
      return response
    } catch (error) {
      record({
        category: 'protocol', operation: command, status: 'failed', message: error?.message,
        measurements: { durationMs: Date.now() - startedAt },
      })
      throw error
    } finally { clearTimeout(timeoutId) }
  }
  return {
    async open() {
      record({ category: 'serial', operation: 'open', status: 'started', measurements: { baudRate } })
      try {
        await port.open({ baudRate, bufferSize: 16384 })
      } catch (error) {
        record({
          category: 'serial', operation: 'open', status: 'failed', message: error?.message,
          measurements: { baudRate },
        })
        throw error
      }
      record({ category: 'serial', operation: 'open', status: 'ready', measurements: { baudRate } })
      if (typeof port.setSignals === 'function') {
        // The E1002 USB-UART bridge maps DTR to boot mode and RTS to reset.
        // Chrome can leave those lines asserted when opening the port, so give
        // the device an explicit normal reset before speaking our protocol.
        try {
          await port.setSignals({ dataTerminalReady: false, requestToSend: false })
          await port.setSignals({ dataTerminalReady: false, requestToSend: true })
          await waitFor(150)
          await port.setSignals({ dataTerminalReady: false, requestToSend: false })
          await waitFor(2_000)
        } catch {
          // Some USB bridges do not expose modem-control lines. The app may
          // already be running, so continue with the protocol connection.
          record({ category: 'serial', operation: 'reset-signals', status: 'unavailable' })
        }
      }
      reader = port.readable.getReader()
      writer = port.writable.getWriter()
      buffered = new Uint8Array(0)
    },
    async request(command, values = {}, requestedTimeout = timeoutMs) {
      const result = requestQueue.then(() => performRequest(command, values, requestedTimeout))
      requestQueue = result.catch(() => {})
      return result
    },
    async close() {
      try { await reader?.cancel() } catch {}
      try { reader?.releaseLock() } catch {}
      try { writer?.releaseLock() } catch {}
      reader = undefined
      writer = undefined
      buffered = new Uint8Array(0)
      try { await port.close() } catch {}
    },
  }
}
