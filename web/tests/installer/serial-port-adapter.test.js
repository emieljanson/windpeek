import { describe, expect, it, vi } from 'vitest'
import { createSerialProtocol, decodeProtocolFrame, encodeProtocolFrame, findGrantedInstallerPort, getSerialSupport, requestInstallerPort, sameInstallerPort } from '../../src/installer/serialPortAdapter'
import { BOARD_IDS } from '../../src/config/configuration'
import { INSTALLER_ERROR_CODES } from '../../src/installer/installerErrors'
import { createInstallerDiagnostics } from '../../src/installer/installerDiagnostics'
import { filterInstallerEvent } from '../../src/installer/sentryReporter'

describe('serial port adapter', () => {
  it('carries numeric device failure details to Sentry without private response fields', async () => {
    const chunks = []
    const diagnostics = createInstallerDiagnostics()
    const releaseCredentials = diagnostics.acquireCredentialLock(['private-network', 'private-password'])
    const reader = { read: async () => ({ done: false, value: chunks.shift() }), cancel: vi.fn(), releaseLock: vi.fn() }
    const writer = { write: async (bytes) => {
      const request = decodeProtocolFrame(bytes)
      chunks.push(encodeProtocolFrame({ requestId: request.requestId, messageType: 2,
        payload: { status: 'ok', apply: 'render_failed', wifi: 'connected', render: 'pending',
          ssid: 'private-network', password: 'private-password',
          diagnostics: { applyError: -1, httpStatus: 429, responseBytes: 200,
            deviceTime: 1787932800, resetReason: 1, latitude: 52.5,
            transportError: 'private-password', arbitrary: 42 } } }))
    }, releaseLock: vi.fn() }
    const port = { open: vi.fn(), readable: { getReader: () => reader }, writable: { getWriter: () => writer } }
    const protocol = createSerialProtocol(port, { diagnostics })
    await protocol.open()
    await protocol.request('get_state')
    releaseCredentials()
    const filtered = filterInstallerEvent({ tags: { 'windpeek.diagnostic': 'installer' },
      extra: { timeline: diagnostics.snapshot().entries } })
    expect(filtered.extra.timeline.at(-1)).toMatchObject({ status: 'render_failed',
      measurements: { applyError: -1, httpStatus: 429, responseBytes: 200, wifiConnected: 1, renderValid: 0 } })
    expect(JSON.stringify(filtered)).not.toMatch(/private-|latitude|arbitrary|transportError/)
  })

  it('supports Firefox desktop when Web Serial is available', () => {
    const requestPort = vi.fn()
    expect(getSerialSupport({
      navigatorApi: { serial: { requestPort }, userAgent: 'Mozilla/5.0 (X11; Linux x86_64; rv:153.0) Gecko/20100101 Firefox/153.0' },
      locationApi: { protocol: 'https:', hostname: 'windpeek.nl' },
    })).toEqual({ supported: true, reason: null })
    expect(requestPort).not.toHaveBeenCalled()
  })

  it('uses direct WebUSB for an E1003 without a system serial port', async () => {
    const device = { vendorId: 0x1a86, productId: 0x7522 }
    const requestDevice = vi.fn().mockResolvedValue(device)
    const getDevices = vi.fn().mockResolvedValue([device])
    const navigatorApi = { usb: { requestDevice, getDevices }, userAgent: 'Dia' }
    expect(getSerialSupport({ navigatorApi, boardId: BOARD_IDS.E1003,
      locationApi: { protocol: 'https:' } })).toEqual({ supported: true, reason: null })
    const selected = await requestInstallerPort(navigatorApi, BOARD_IDS.E1003)
    expect(requestDevice).toHaveBeenCalledWith({ filters: [{ vendorId: 0x1a86, productId: 0x7522 }] })
    const granted = await findGrantedInstallerPort({ navigatorApi, boardId: BOARD_IDS.E1003,
      classify: (candidate) => sameInstallerPort(candidate, selected) })
    expect(sameInstallerPort(granted, selected)).toBe(true)
  })

  it('blocks mobile, insecure and unsupported browsers before permission', () => {
    const requestPort = vi.fn()
    expect(getSerialSupport({ navigatorApi: { serial: { requestPort }, userAgent: 'iPhone' }, locationApi: { protocol: 'https:', hostname: 'windpeek.nl' } }).reason).toBe('desktop-required')
    expect(getSerialSupport({ navigatorApi: { serial: { requestPort }, userAgent: 'Chrome' }, locationApi: { protocol: 'http:', hostname: 'windpeek.nl' } }).reason).toBe('secure-context-required')
    expect(getSerialSupport({ navigatorApi: { userAgent: 'Safari' }, locationApi: { protocol: 'https:', hostname: 'windpeek.nl' } }).reason).toBe('browser-not-supported')
    expect(requestPort).not.toHaveBeenCalled()
  })

  it('treats chooser cancellation as a normal empty result', async () => {
    const error = Object.assign(new Error('No port selected'), { name: 'NotFoundError' })
    const navigatorApi = { serial: { requestPort: vi.fn().mockRejectedValue(error) }, userAgent: 'Chrome' }
    await expect(requestInstallerPort(navigatorApi)).resolves.toBeNull()
  })

  it('frames a JSON request with the protocol CRC', () => {
    const input = { requestId: 42, payload: { command: 'hello' } }
    expect(decodeProtocolFrame(encodeProtocolFrame(input))).toMatchObject(input)
    const corrupt = encodeProtocolFrame(input); corrupt[corrupt.length - 1] ^= 1
    expect(() => decodeProtocolFrame(corrupt)).toThrow(/invalid/i)
  })

  it('reconnects only to a granted port that passes classification', async () => {
    const ports = [{ id: 1 }, { id: 2 }]
    const result = await findGrantedInstallerPort({ navigatorApi: { serial: { getPorts: async () => ports } }, classify: async (port) => port.id === 2 })
    expect(result).toBe(ports[1])
  })

  it('boots an E1002 into its app before opening the installer protocol', async () => {
    const waitFor = vi.fn().mockResolvedValue(undefined)
    const reader = { cancel: vi.fn(), releaseLock: vi.fn() }
    const writer = { releaseLock: vi.fn() }
    const port = {
      open: vi.fn(),
      setSignals: vi.fn(),
      readable: { getReader: () => reader },
      writable: { getWriter: () => writer },
    }

    const protocol = createSerialProtocol(port, { waitFor })
    await protocol.open()

    expect(port.setSignals.mock.calls).toEqual([
      [{ dataTerminalReady: false, requestToSend: false }],
      [{ dataTerminalReady: false, requestToSend: true }],
      [{ dataTerminalReady: false, requestToSend: false }],
    ])
    expect(waitFor.mock.calls).toEqual([[150], [2_000]])
  })

  it('continues when modem-control signals are unavailable', async () => {
    const responses = []
    const diagnostics = { record: vi.fn() }
    const reader = {
      read: vi.fn(async () => ({ done: false, value: responses.shift() })),
      cancel: vi.fn(),
      releaseLock: vi.fn(),
    }
    const writer = {
      write: vi.fn(async (bytes) => {
        const request = decodeProtocolFrame(bytes)
        responses.push(encodeProtocolFrame({
          requestId: request.requestId,
          messageType: 2,
          payload: { status: 'ok' },
        }))
      }),
      releaseLock: vi.fn(),
    }
    const port = {
      open: vi.fn(),
      setSignals: vi.fn().mockRejectedValue(new Error('not supported')),
      readable: { getReader: () => reader },
      writable: { getWriter: () => writer },
    }
    const protocol = createSerialProtocol(port, { diagnostics })

    await protocol.open()
    await expect(protocol.request('hello')).resolves.toEqual({ status: 'ok' })

    expect(diagnostics.record).toHaveBeenCalledWith({
      category: 'serial', operation: 'reset-signals', status: 'unavailable',
    })
  })

  it('records why opening an already busy USB port failed', async () => {
    const diagnostics = { record: vi.fn() }
    const port = { open: vi.fn().mockRejectedValue(new Error('Failed to open serial port')) }
    const protocol = createSerialProtocol(port, { diagnostics })

    await expect(protocol.open()).rejects.toThrow('Failed to open serial port')
    expect(diagnostics.record).toHaveBeenLastCalledWith({
      category: 'serial',
      operation: 'open',
      status: 'failed',
      message: 'Failed to open serial port',
      measurements: { baudRate: 115200 },
    })
  })

  it('serializes overlapping protocol requests on one reader', async () => {
    const responses = []
    const commands = []
    const reader = {
      read: vi.fn(async () => ({ done: false, value: responses.shift() })),
      cancel: vi.fn(),
      releaseLock: vi.fn(),
    }
    const writer = {
      write: vi.fn(async (bytes) => {
        const request = decodeProtocolFrame(bytes)
        commands.push(request.payload.command)
        responses.push(encodeProtocolFrame({ requestId: request.requestId, messageType: 2, payload: { status: 'ok' } }))
      }),
      releaseLock: vi.fn(),
    }
    const port = {
      open: vi.fn(), close: vi.fn(),
      readable: { getReader: () => reader },
      writable: { getWriter: () => writer },
    }
    const protocol = createSerialProtocol(port)
    await protocol.open()
    await Promise.all([protocol.request('scan_networks'), protocol.request('begin')])
    expect(commands).toEqual(['scan_networks', 'begin'])
    expect(reader.read).toHaveBeenCalledTimes(2)
  })

  it('ignores UART logs and accepts a frame whose magic spans chunks', async () => {
    const chunks = []
    const diagnostics = { record: vi.fn() }
    const reader = { read: vi.fn(async () => ({ done: false, value: chunks.shift() })), cancel: vi.fn(), releaseLock: vi.fn() }
    const writer = {
      write: vi.fn(async (bytes) => {
        const request = decodeProtocolFrame(bytes)
        const response = encodeProtocolFrame({ requestId: request.requestId, messageType: 2, payload: { status: 'ok' } })
        chunks.push(new TextEncoder().encode('I (123) main: ordinary boot log\r\nWIN'))
        chunks.push(new Uint8Array([...new TextEncoder().encode('DSC01'), ...response.slice(8)]))
      }),
      releaseLock: vi.fn(),
    }
    const port = { open: vi.fn(), close: vi.fn(), readable: { getReader: () => reader }, writable: { getWriter: () => writer } }
    const protocol = createSerialProtocol(port, { diagnostics })
    await protocol.open()
    await expect(protocol.request('hello')).resolves.toEqual({ status: 'ok' })
    expect(JSON.stringify(diagnostics.record.mock.calls)).not.toContain('ordinary boot log')
    expect(JSON.stringify(diagnostics.record.mock.calls)).not.toContain('payload')
  })

  it('consumes delayed frames and preserves another frame from the same serial chunk', async () => {
    const chunks = []
    const reader = { read: vi.fn(async () => ({ done: false, value: chunks.shift() })), cancel: vi.fn(), releaseLock: vi.fn() }
    const writer = {
      write: vi.fn(async (bytes) => {
        const request = decodeProtocolFrame(bytes)
        if (request.requestId !== 1) return
        const delayed = encodeProtocolFrame({ requestId: 99, messageType: 2, payload: { status: 'old' } })
        const first = encodeProtocolFrame({ requestId: 1, messageType: 2, payload: { status: 'first' } })
        const second = encodeProtocolFrame({ requestId: 2, messageType: 2, payload: { status: 'second' } })
        chunks.push(new Uint8Array([...delayed, ...first, ...second]))
      }),
      releaseLock: vi.fn(),
    }
    const port = { open: vi.fn(), close: vi.fn(), readable: { getReader: () => reader }, writable: { getWriter: () => writer } }
    const protocol = createSerialProtocol(port)
    await protocol.open()
    await expect(protocol.request('hello')).resolves.toEqual({ status: 'first' })
    await expect(protocol.request('get_state')).resolves.toEqual({ status: 'second' })
    expect(reader.read).toHaveBeenCalledTimes(1)
  })

  it('cancels and fully releases a timed-out serial connection', async () => {
    vi.useFakeTimers()
    try {
      const reader = {
        read: vi.fn(() => new Promise(() => {})),
        cancel: vi.fn().mockResolvedValue(undefined),
        releaseLock: vi.fn(),
      }
      const writer = {
        write: vi.fn().mockResolvedValue(undefined),
        releaseLock: vi.fn(),
      }
      const port = {
        open: vi.fn(), close: vi.fn(),
        readable: { getReader: () => reader },
        writable: { getWriter: () => writer },
      }
      const protocol = createSerialProtocol(port, { timeoutMs: 10 })
      await protocol.open()
      const request = protocol.request('hello')
      const rejection = expect(request).rejects.toMatchObject({
        code: INSTALLER_ERROR_CODES.CONNECTION_LOST,
      })

      await vi.advanceTimersByTimeAsync(10)
      await rejection
      expect(reader.cancel).toHaveBeenCalledOnce()

      await protocol.close()
      expect(reader.releaseLock).toHaveBeenCalledOnce()
      expect(writer.releaseLock).toHaveBeenCalledOnce()
      expect(port.close).toHaveBeenCalledOnce()
    } finally {
      vi.useRealTimers()
    }
  })
})
