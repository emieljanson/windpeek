export const INSTALLER_ERROR_CODES = Object.freeze({
  UNSUPPORTED: 'unsupported',
  CANCELLED: 'cancelled',
  DEVICE_NOT_ALLOWED: 'device-not-allowed',
  CONNECTION_LOST: 'connection-lost',
  INVALID_RESPONSE: 'invalid-response',
  INCOMPATIBLE_DEVICE: 'incompatible-device',
  UNCONFIRMED_DEVICE: 'unconfirmed-device',
  INVALID_MANIFEST: 'invalid-manifest',
  DOWNLOAD_FAILED: 'download-failed',
  FLASH_FAILED: 'flash-failed',
  WIFI_FAILED: 'wifi-failed',
  VERIFICATION_FAILED: 'verification-failed',
})

export class InstallerError extends Error {
  constructor(code, message, options = {}) {
    super(message, options.cause ? { cause: options.cause } : undefined)
    this.name = 'InstallerError'
    this.code = code
    this.recoverable = options.recoverable ?? true
    this.safeToDisconnect = options.safeToDisconnect ?? true
  }
}
export function isChooserCancellation(error) {
  return error?.name === 'NotFoundError' || error?.name === 'AbortError'
}

export function asInstallerError(error, fallbackCode, fallbackMessage, options = {}) {
  if (error instanceof InstallerError) return error
  return new InstallerError(fallbackCode, fallbackMessage, { ...options, cause: error })
}

export function throwIfHardwareUnavailable(response, recordFailure) {
  if (response.status !== 'hardware_unavailable') return
  recordFailure({ applyError: response.code })
  throw new InstallerError(INSTALLER_ERROR_CODES.VERIFICATION_FAILED,
    'The display could not initialize. Reconnect your device and try again.')
}
