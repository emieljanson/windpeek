import { describe, expect, it, vi } from 'vitest'
import { createEsptoolAdapter } from '../../src/installer/esptoolAdapter'
import { INSTALLER_ERROR_CODES } from '../../src/installer/installerErrors'

describe('esptool adapter', () => {
  it('offers reconnection when both bootloader connection attempts time out', async () => {
    const connectionError = new Error('Failed to connect with the device')
    const disconnect = vi.fn().mockResolvedValue(undefined)
    class Transport { disconnect = disconnect }
    class ESPLoader {
      main = vi.fn().mockRejectedValue(connectionError)
    }
    const adapter = createEsptoolAdapter({ moduleLoader: async () => ({ Transport, ESPLoader }) })

    await expect(adapter.identify({})).rejects.toMatchObject({
      code: INSTALLER_ERROR_CODES.CONNECTION_LOST,
      message: 'Could not connect to the device. Reconnect it and try again.',
      recoverable: true,
      safeToDisconnect: true,
      cause: connectionError,
    })
    expect(disconnect).toHaveBeenCalledTimes(2)
  })

  it('enters the bootloader, verifies writes with MD5, resets and disconnects', async () => {
    const diagnostics = { record: vi.fn() }
    const disconnect = vi.fn().mockResolvedValue(undefined)
    class Transport {
      constructor(port, tracing) { this.port = port; this.tracing = tracing }
      disconnect = disconnect
    }
    const loader = {
      main: vi.fn().mockResolvedValue('ESP32-S3'),
      writeFlash: vi.fn(async (options) => {
        expect(options.calculateMD5Hash(new Uint8Array([1, 2]))).toBe('verified-md5')
      }),
      after: vi.fn(),
    }
    class ESPLoader {
      constructor({ terminal }) {
        terminal.writeLine('Connecting to ESP32-S3')
        return loader
      }
    }
    const SparkMD5 = { ArrayBuffer: { hash: vi.fn(() => 'verified-md5') } }
    const adapter = createEsptoolAdapter({ diagnostics, moduleLoader: async () => ({ Transport, ESPLoader, SparkMD5 }) })

    const identity = await adapter.identify({ id: 'port' })
    expect(loader.main).toHaveBeenCalledWith('default_reset')
    await adapter.flash({ ...identity, bundle: { eraseFlash: false, parts: [{ offset: 0, data: new Uint8Array([1, 2]) }] } })
    expect(loader.writeFlash).toHaveBeenCalledOnce()
    expect(loader.after).toHaveBeenCalledWith(
      'custom_reset',
      undefined,
      'D0|R1|W100|R0|W500|D0',
    )
    expect(disconnect).toHaveBeenCalledOnce()
    expect(diagnostics.record).toHaveBeenCalledWith(expect.objectContaining({
      category: 'bootloader', operation: 'terminal', message: 'Connecting to ESP32-S3',
    }))
  })

  it('reports a failed write as safe after transport cleanup', async () => {
    const disconnect = vi.fn().mockResolvedValue(undefined)
    class Transport { disconnect = disconnect }
    const loader = { main: vi.fn().mockResolvedValue('ESP32-S3'), writeFlash: vi.fn().mockRejectedValue(new Error('lost')), after: vi.fn() }
    class ESPLoader { constructor() { return loader } }
    const adapter = createEsptoolAdapter({ moduleLoader: async () => ({ Transport, ESPLoader }) })
    const identity = await adapter.identify({})

    await expect(adapter.flash({ ...identity, bundle: { eraseFlash: false, parts: [] } }))
      .rejects.toMatchObject({ code: INSTALLER_ERROR_CODES.FLASH_FAILED, safeToDisconnect: true })
    expect(disconnect).toHaveBeenCalledOnce()
  })

  it('falls back to the safe baud rate when fast bootloader setup fails', async () => {
    const disconnects = []
    const baudRates = []
    class Transport {
      constructor() {
        const disconnect = vi.fn().mockResolvedValue(undefined)
        disconnects.push(disconnect)
        this.disconnect = disconnect
      }
    }
    class ESPLoader {
      constructor({ baudrate, transport }) {
        baudRates.push(baudrate)
        return {
          transport,
          main: baudrate === 460800
            ? vi.fn().mockRejectedValue(new Error('fast baud unavailable'))
            : vi.fn().mockResolvedValue('ESP32-S3'),
        }
      }
    }
    const adapter = createEsptoolAdapter({ moduleLoader: async () => ({ Transport, ESPLoader }) })

    const identity = await adapter.identify({})

    expect(identity.chipFamily).toBe('ESP32-S3')
    expect(baudRates).toEqual([460800, 115200])
    expect(disconnects[0]).toHaveBeenCalledOnce()
    expect(disconnects[1]).not.toHaveBeenCalled()
  })
})
