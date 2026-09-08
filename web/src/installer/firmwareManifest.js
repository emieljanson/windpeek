import { InstallerError, INSTALLER_ERROR_CODES } from './installerErrors'
import { BOARD_ID, BOARD_IDS, CONFIGURATION_VERSION } from '../config/configuration'

export const CHIP_FAMILY = 'ESP32-S3'
export const FIRMWARE_DOWNLOAD_TIMEOUT_MS = 30_000
const FLASH_SIZE = 32 * 1024 * 1024
const SHA256_PATTERN = /^[a-f0-9]{64}$/
const REQUIRED_KINDS = ['bootloader', 'partition-table', 'boot-selection', 'application']
export const FIRMWARE_BASE_URL = import.meta.env.VITE_FIRMWARE_BASE_URL || './firmware/'

export function installerReleaseBoardId(boardId) {
  return boardId === BOARD_IDS.E1001 ? BOARD_IDS.E1002 : boardId
}

function fail(message) {
  throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_MANIFEST, message)
}

function validatePart(part, flashSize) {
  if (!part || !REQUIRED_KINDS.includes(part.kind) || typeof part.file !== 'string' ||
      !Number.isInteger(part.offset) || !Number.isInteger(part.size) || part.size <= 0 ||
      part.offset < 0 || part.offset + part.size > flashSize ||
      typeof part.sha256 !== 'string' || !SHA256_PATTERN.test(part.sha256)) {
    fail('The firmware release contains an invalid flash part.')
  }
}

function assertNoOverlap(parts) {
  const sorted = [...parts].sort((a, b) => a.offset - b.offset)
  for (let index = 1; index < sorted.length; index += 1) {
    if (sorted[index].offset < sorted[index - 1].offset + sorted[index - 1].size) {
      fail('The firmware release contains overlapping flash ranges.')
    }
  }
}

export function validateFirmwareManifest(manifest, expectedBoardId = BOARD_ID) {
  if (!manifest || manifest.schemaVersion !== 1 || manifest.boardId !== expectedBoardId ||
      manifest.chipFamily !== CHIP_FAMILY || manifest.flashSize !== FLASH_SIZE ||
      !Number.isInteger(manifest.firmwareLayoutVersion) || manifest.firmwareLayoutVersion < 1 ||
      !manifest.protocol || manifest.protocol.minimum > 1 || manifest.protocol.maximum < 1 ||
      !manifest.configuration || manifest.configuration.minimum > CONFIGURATION_VERSION ||
      manifest.configuration.maximum < CONFIGURATION_VERSION) {
    fail('This firmware release is not compatible with Windpeek.')
  }
  if (!Array.isArray(manifest.parts) || manifest.parts.length !== REQUIRED_KINDS.length ||
      new Set(manifest.parts.map((part) => part.kind)).size !== REQUIRED_KINDS.length) {
    fail('The firmware release is incomplete.')
  }
  manifest.parts.forEach((part) => validatePart(part, manifest.flashSize))
  assertNoOverlap(manifest.parts)
  const canonical = new Map(manifest.parts.map((part) => [part.kind, part]))
  for (const mode of ['cleanInstall', 'preservingUpdate']) {
    const set = manifest[mode]
    if (!set || !Array.isArray(set.parts)) fail('The firmware write plan is missing.')
    const kinds = set.parts.map((part) => part.kind)
    if (mode === 'cleanInstall' &&
        (kinds.length !== REQUIRED_KINDS.length || new Set(kinds).size !== REQUIRED_KINDS.length ||
         REQUIRED_KINDS.some((kind) => !kinds.includes(kind)))) {
      fail('The clean-install plan is incomplete.')
    }
    set.parts.forEach((part) => {
      validatePart(part, manifest.flashSize)
      if (JSON.stringify(part) !== JSON.stringify(canonical.get(part.kind))) {
        fail('The firmware write plan does not match its verified files.')
      }
    })
    assertNoOverlap(set.parts)
  }
  if (manifest.cleanInstall.eraseFlash !== true || manifest.preservingUpdate.eraseFlash !== false ||
      manifest.preservingUpdate.parts.map((part) => part.kind).join(',') !== 'boot-selection,application') {
    fail('The firmware erase policy is unsafe.')
  }
  return manifest
}

function toHex(buffer) {
  return [...new Uint8Array(buffer)].map((byte) => byte.toString(16).padStart(2, '0')).join('')
}

export async function verifyFirmwareBytes(part, bytes, cryptoApi = globalThis.crypto) {
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes)
  if (data.byteLength !== part.size || !cryptoApi?.subtle) {
    fail('A downloaded firmware file is incomplete.')
  }
  const digest = toHex(await cryptoApi.subtle.digest('SHA-256', data))
  if (digest !== part.sha256) fail('A downloaded firmware file failed verification.')
  return data
}

async function fetchJson(fetchFn, url, signal) {
  let response
  try {
    response = await fetchFn(url, { cache: 'no-store', signal })
  } catch (error) {
    throw new InstallerError(INSTALLER_ERROR_CODES.DOWNLOAD_FAILED, 'The firmware release could not be downloaded.', { cause: error })
  }
  if (!response.ok) {
    throw new InstallerError(INSTALLER_ERROR_CODES.DOWNLOAD_FAILED, 'The firmware release could not be downloaded.')
  }
  return response.json()
}

async function withDownloadTimeout(operation, callerSignal) {
  const controller = new AbortController()
  let timedOut = false
  const abortFromCaller = () => controller.abort(callerSignal.reason)
  if (callerSignal?.aborted) abortFromCaller()
  else callerSignal?.addEventListener('abort', abortFromCaller, { once: true })

  let rejectTimeout
  const timeoutFailure = new Promise((_, reject) => { rejectTimeout = reject })
  const timer = setTimeout(() => {
    timedOut = true
    controller.abort()
    rejectTimeout(new InstallerError(
      INSTALLER_ERROR_CODES.DOWNLOAD_FAILED,
      'The firmware download took too long. Please check your connection and try again.',
    ))
  }, FIRMWARE_DOWNLOAD_TIMEOUT_MS)

  try {
    return await Promise.race([operation(controller.signal), timeoutFailure])
  } catch (error) {
    if (timedOut) {
      throw new InstallerError(
        INSTALLER_ERROR_CODES.DOWNLOAD_FAILED,
        'The firmware download took too long. Please check your connection and try again.',
        { cause: error },
      )
    }
    throw error
  } finally {
    clearTimeout(timer)
    callerSignal?.removeEventListener('abort', abortFromCaller)
  }
}

function releaseUrl(path, base, message) {
  const url = new URL(path, base)
  if (url.origin !== base.origin || !url.pathname.startsWith(base.pathname)) fail(message)
  return url
}

export async function loadFirmwareRelease({
  boardId = BOARD_ID,
  baseUrl = FIRMWARE_BASE_URL,
  fetchFn = globalThis.fetch,
  cryptoApi = globalThis.crypto,
  signal,
} = {}) {
  return withDownloadTimeout(async (downloadSignal) => {
    const releaseBoardId = installerReleaseBoardId(boardId)
    const root = new URL(baseUrl, globalThis.location?.href ?? 'https://windpeek.invalid/')
    const pointerPath = releaseBoardId === BOARD_IDS.E1003 ? 'e1003/latest.json' : 'latest.json'
    const pointerUrl = new URL(pointerPath, root)
    const pointer = await fetchJson(fetchFn, pointerUrl, downloadSignal)
    if (!pointer || typeof pointer.manifest !== 'string' || !SHA256_PATTERN.test(pointer.sha256 ?? '')) fail('The release pointer is invalid.')
    const pointerDirectory = new URL('.', pointerUrl)
    const manifestUrl = releaseUrl(pointer.manifest, pointerDirectory, 'The release pointer leaves the Windpeek firmware directory.')
    const response = await fetchFn(manifestUrl, { cache: 'no-store', signal: downloadSignal })
    if (!response.ok) throw new InstallerError(INSTALLER_ERROR_CODES.DOWNLOAD_FAILED, 'The firmware manifest could not be downloaded.')
    const manifestBytes = new Uint8Array(await response.arrayBuffer())
    const manifestDigest = toHex(await cryptoApi.subtle.digest('SHA-256', manifestBytes))
    if (manifestDigest !== pointer.sha256) fail('The firmware manifest failed verification.')
    let manifest
    try {
      manifest = validateFirmwareManifest(JSON.parse(new TextDecoder().decode(manifestBytes)), releaseBoardId)
    } catch (error) {
      if (error instanceof InstallerError) throw error
      fail('The firmware manifest is not valid JSON.')
    }
    if (pointer.version !== manifest.version) fail('The firmware release versions do not match.')
    return { manifest, manifestUrl }
  }, signal)
}

export async function loadFirmwareParts({ manifest, manifestUrl, mode, boardId = BOARD_ID, fetchFn = globalThis.fetch, cryptoApi = globalThis.crypto, signal }) {
  return withDownloadTimeout(async (downloadSignal) => {
    validateFirmwareManifest(manifest, installerReleaseBoardId(boardId))
    const writeSet = manifest[mode]
    if (!writeSet) fail('Unknown firmware write mode.')
    const parts = await Promise.all(writeSet.parts.map(async (part) => {
      const base = new URL('.', manifestUrl)
      const partUrl = releaseUrl(part.file, base, 'A firmware file leaves its release directory.')
      const response = await fetchFn(partUrl, { cache: 'no-store', signal: downloadSignal })
      if (!response.ok) throw new InstallerError(INSTALLER_ERROR_CODES.DOWNLOAD_FAILED, 'A firmware file could not be downloaded.')
      return { ...part, data: await verifyFirmwareBytes(part, await response.arrayBuffer(), cryptoApi) }
    }))
    return { eraseFlash: writeSet.eraseFlash, parts }
  }, signal)
}
