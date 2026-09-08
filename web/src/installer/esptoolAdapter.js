import { InstallerError, INSTALLER_ERROR_CODES } from './installerErrors'

export function createEsptoolAdapter({
  diagnostics,
  moduleLoader = async () => {
    const [esptool, sparkMd5] = await Promise.all([import('esptool-js'), import('spark-md5')])
    return { ...esptool, SparkMD5: sparkMd5.default }
  },
} = {}) {
  let calculateMD5Hash

  function record(entry) {
    try { diagnostics?.record?.(entry) } catch {}
  }

  function diagnosticTerminal(terminal) {
    const emit = (message) => record({ category: 'bootloader', operation: 'terminal', status: 'output', message })
    return {
      clean() { try { terminal?.clean?.() } catch {} },
      writeLine(message) { emit(message); try { terminal?.writeLine?.(message) } catch {} },
      write(message) { emit(message); try { terminal?.write?.(message) } catch {} },
    }
  }

  return {
    async identify(port, terminal) {
      const { Transport, ESPLoader, SparkMD5 } = await moduleLoader()
      calculateMD5Hash = SparkMD5
        ? (image) => SparkMD5.ArrayBuffer.hash(
            image.byteOffset === 0 && image.byteLength === image.buffer.byteLength
              ? image.buffer
              : image.buffer.slice(image.byteOffset, image.byteOffset + image.byteLength),
          )
        : undefined
      const connect = async (baudrate) => {
        const transport = new Transport(port, false)
        const loader = new ESPLoader({ transport, baudrate, terminal: diagnosticTerminal(terminal) })
        try {
          const chip = await loader.main('default_reset')
          record({ category: 'bootloader', operation: 'baudrate', status: 'ready', measurements: { baudRate: baudrate } })
          return { chip, loader, transport }
        } catch (error) {
          await transport.disconnect().catch(() => {})
          throw error
        }
      }
      try {
        let identity
        try {
          identity = await connect(460800)
        } catch (fastError) {
          record({
            category: 'bootloader', operation: 'baudrate', status: 'fallback',
            message: fastError?.message, measurements: { baudRate: 115200 },
          })
          identity = await connect(115200)
        }
        return {
          chipFamily: /ESP32-S3/i.test(identity.chip) ? 'ESP32-S3' : identity.chip,
          loader: identity.loader,
          transport: identity.transport,
        }
      } catch (error) {
        throw new InstallerError(INSTALLER_ERROR_CODES.INCOMPATIBLE_DEVICE, 'This USB device is not a compatible Windpeek.', { cause: error })
      }
    },
    async flash({ loader, transport, bundle, onProgress = () => {} }) {
      try {
        if (bundle.eraseFlash) await loader.eraseFlash()
        const fileArray = bundle.parts.map((part) => ({ data: part.data, address: part.offset }))
        await loader.writeFlash({
          fileArray,
          flashSize: 'keep',
          flashMode: 'keep',
          flashFreq: 'keep',
          eraseAll: false,
          compress: true,
          reportProgress: (fileIndex, written, total) => {
            record({
              category: 'flash', operation: 'write', status: 'progress',
              measurements: { fileIndex, writtenBytes: written, totalBytes: total },
            })
            onProgress({ fileIndex, written, total })
          },
          calculateMD5Hash,
        })
        await loader.after('custom_reset', undefined, 'D0|R1|W100|R0|W500|D0')
      } catch (error) {
        throw new InstallerError(INSTALLER_ERROR_CODES.FLASH_FAILED, 'Firmware installation was interrupted. Reconnect the device to try again.', { cause: error, safeToDisconnect: true })
      } finally {
        await transport?.disconnect().catch(() => {})
      }
    },
  }
}
