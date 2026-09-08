import { createHash, webcrypto } from 'node:crypto'
import { describe, expect, it, vi } from 'vitest'
import { BOARD_IDS, CONFIGURATION_VERSION } from '../../src/config/configuration'
import {
  FIRMWARE_BASE_URL,
  FIRMWARE_DOWNLOAD_TIMEOUT_MS,
  installerReleaseBoardId,
  loadFirmwareParts,
  loadFirmwareRelease,
  validateFirmwareManifest,
  verifyFirmwareBytes,
} from '../../src/installer/firmwareManifest'

function sha(bytes) { return createHash('sha256').update(bytes).digest('hex') }
function part(kind, offset, bytes = new Uint8Array([1, 2, 3])) {
  return { kind, file: `${kind}.bin`, offset, size: bytes.length, sha256: sha(bytes) }
}
function manifest() {
  const parts = [part('bootloader', 0), part('partition-table', 0x8000), part('boot-selection', 0xf000), part('application', 0x20000)]
  return {
    schemaVersion: 1, version: '2.0.0', boardId: 'seeedstudio_reterminal_e1002', chipFamily: 'ESP32-S3', firmwareLayoutVersion: 1, flashSize: 32 * 1024 * 1024,
    protocol: { minimum: 1, maximum: 1 }, configuration: { minimum: CONFIGURATION_VERSION, maximum: CONFIGURATION_VERSION }, parts,
    cleanInstall: { eraseFlash: true, parts: parts.map((item) => ({ ...item })) },
    preservingUpdate: { eraseFlash: false, parts: parts.filter((item) => ['boot-selection', 'application'].includes(item.kind)).map((item) => ({ ...item })) },
  }
}

describe('firmware manifest', () => {
  it('loads firmware relative to the deployed configurator', () => {
    expect(FIRMWARE_BASE_URL).toBe('./firmware/')
  })

  it('accepts the exact E1002 release contract', () => {
    expect(validateFirmwareManifest(manifest()).version).toBe('2.0.0')
  })

  it('uses the shared E1002 release identity for E1001', () => {
    expect(installerReleaseBoardId(BOARD_IDS.E1001)).toBe(BOARD_IDS.E1002)
  })

  it('loads the shared E1002 firmware write set for E1001', async () => {
    const source = manifest()
    const manifestBytes = new TextEncoder().encode(JSON.stringify(source))
    const pointer = {
      version: source.version,
      manifest: `${source.version}/installer-manifest.json`,
      sha256: sha(manifestBytes),
    }
    const fetchFn = vi.fn(async (url) => {
      if (String(url).endsWith('latest.json')) return { ok: true, json: async () => pointer }
      if (String(url).endsWith('installer-manifest.json')) {
        return { ok: true, arrayBuffer: async () => manifestBytes.buffer }
      }
      return { ok: true, arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer }
    })

    const release = await loadFirmwareRelease({
      baseUrl: 'https://example.test/firmware/',
      boardId: BOARD_IDS.E1001,
      fetchFn,
      cryptoApi: webcrypto,
    })
    const bundle = await loadFirmwareParts({
      ...release,
      mode: 'preservingUpdate',
      boardId: BOARD_IDS.E1001,
      fetchFn,
      cryptoApi: webcrypto,
    })

    expect(fetchFn.mock.calls[0][0]).toEqual(new URL('https://example.test/firmware/latest.json'))
    expect(release.manifest.boardId).toBe(BOARD_IDS.E1002)
    expect(bundle.eraseFlash).toBe(false)
    expect(bundle.parts.map((item) => item.kind)).toEqual(['boot-selection', 'application'])
  })

  it('rejects incompatible identity, overlapping ranges and unsafe update plans', () => {
    expect(() => validateFirmwareManifest({ ...manifest(), boardId: 'other' })).toThrow(/not compatible/i)
    expect(() => validateFirmwareManifest({ ...manifest(), firmwareLayoutVersion: undefined })).toThrow(/not compatible/i)
    const overlap = manifest(); overlap.parts[1].offset = 1; overlap.cleanInstall.parts[1].offset = 1
    expect(() => validateFirmwareManifest(overlap)).toThrow(/overlapping/i)
    const erase = manifest(); erase.preservingUpdate.eraseFlash = true
    expect(() => validateFirmwareManifest(erase)).toThrow(/erase policy/i)
    const emptyClean = manifest(); emptyClean.cleanInstall.parts = []
    expect(() => validateFirmwareManifest(emptyClean)).toThrow(/clean-install plan is incomplete/i)
    const duplicateClean = manifest(); duplicateClean.cleanInstall.parts[3] = { ...duplicateClean.cleanInstall.parts[0] }
    expect(() => validateFirmwareManifest(duplicateClean)).toThrow(/clean-install plan is incomplete/i)
  })

  it('verifies size and SHA-256 before returning bytes', async () => {
    const bytes = new Uint8Array([1, 2, 3])
    await expect(verifyFirmwareBytes(part('application', 0, bytes), bytes, webcrypto)).resolves.toEqual(bytes)
    await expect(verifyFirmwareBytes(part('application', 0, bytes), new Uint8Array([9, 9, 9]), webcrypto)).rejects.toThrow(/verification/i)
  })

  it('downloads only the requested verified write set', async () => {
    const source = manifest()
    const fetchFn = vi.fn(async () => ({ ok: true, arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer }))
    const bundle = await loadFirmwareParts({ manifest: source, manifestUrl: new URL('https://example.test/2/installer-manifest.json'), mode: 'preservingUpdate', fetchFn, cryptoApi: webcrypto })
    expect(bundle.eraseFlash).toBe(false)
    expect(bundle.parts.map((item) => item.kind)).toEqual(['boot-selection', 'application'])
    expect(fetchFn).toHaveBeenCalledTimes(2)
  })

  it('loads a release only when the immutable manifest matches the pointer digest', async () => {
    const source = manifest()
    const manifestBytes = new TextEncoder().encode(JSON.stringify(source))
    const pointer = { version: source.version, manifest: `installer-manifest-${source.version}.json`, sha256: sha(manifestBytes) }
    const fetchFn = vi.fn(async (url) => String(url).endsWith('latest.json')
      ? { ok: true, json: async () => pointer }
      : { ok: true, arrayBuffer: async () => manifestBytes.buffer })

    await expect(loadFirmwareRelease({
      baseUrl: 'https://example.test/firmware/', fetchFn, cryptoApi: webcrypto,
    })).resolves.toMatchObject({
      manifest: { version: source.version },
      manifestUrl: new URL(`https://example.test/firmware/installer-manifest-${source.version}.json`),
    })
  })

  it('resolves an E1003 manifest relative to its board-specific pointer', async () => {
    const source = { ...manifest(), boardId: BOARD_IDS.E1003 }
    const manifestBytes = new TextEncoder().encode(JSON.stringify(source))
    const pointer = {
      version: source.version,
      manifest: `${source.version}/installer-manifest.json`,
      sha256: sha(manifestBytes),
    }
    const fetchFn = vi.fn(async (url) => String(url).endsWith('latest.json')
      ? { ok: true, json: async () => pointer }
      : { ok: true, arrayBuffer: async () => manifestBytes.buffer })

    const release = await loadFirmwareRelease({
      baseUrl: 'https://example.test/firmware/',
      boardId: BOARD_IDS.E1003,
      fetchFn,
      cryptoApi: webcrypto,
    })

    expect(release.manifestUrl).toEqual(new URL(
      'https://example.test/firmware/e1003/2.0.0/installer-manifest.json',
    ))
  })

  it('rejects a changed manifest and a pointer outside the firmware directory', async () => {
    const source = manifest()
    const manifestBytes = new TextEncoder().encode(JSON.stringify(source))
    const fetchFn = vi.fn(async (url) => String(url).endsWith('latest.json')
      ? { ok: true, json: async () => ({ version: source.version, manifest: `${source.version}/installer-manifest.json`, sha256: '0'.repeat(64) }) }
      : { ok: true, arrayBuffer: async () => manifestBytes.buffer })
    await expect(loadFirmwareRelease({
      baseUrl: 'https://example.test/firmware/', fetchFn, cryptoApi: webcrypto,
    })).rejects.toThrow(/failed verification/i)

    const escapingFetch = vi.fn(async () => ({
      ok: true,
      json: async () => ({ version: source.version, manifest: 'https://untrusted.example/manifest.json', sha256: sha(manifestBytes) }),
    }))
    await expect(loadFirmwareRelease({
      baseUrl: 'https://example.test/firmware/', fetchFn: escapingFetch, cryptoApi: webcrypto,
    })).rejects.toThrow(/leaves the Windpeek firmware directory/i)
  })

  it('rejects firmware part URLs outside the immutable release directory', async () => {
    const source = manifest()
    for (const writeSet of [source.parts, source.cleanInstall.parts, source.preservingUpdate.parts]) {
      const bootSelection = writeSet.find((item) => item.kind === 'boot-selection')
      if (bootSelection) bootSelection.file = 'https://untrusted.example/boot.bin'
    }
    await expect(loadFirmwareParts({ manifest: source, manifestUrl: new URL('https://example.test/2/installer-manifest.json'), mode: 'preservingUpdate', fetchFn: vi.fn(), cryptoApi: webcrypto })).rejects.toThrow(/release directory/i)
  })

  it('times out a firmware request that never settles', async () => {
    vi.useFakeTimers()
    try {
      const pending = loadFirmwareRelease({
        baseUrl: 'https://example.test/firmware/',
        fetchFn: vi.fn(() => new Promise(() => {})),
        cryptoApi: webcrypto,
      })
      const rejection = expect(pending).rejects.toMatchObject({ code: 'download-failed' })
      await vi.advanceTimersByTimeAsync(FIRMWARE_DOWNLOAD_TIMEOUT_MS)
      await rejection
    } finally {
      vi.useRealTimers()
    }
  })

  it('times out a response body that never settles', async () => {
    vi.useFakeTimers()
    try {
      const source = manifest()
      const pending = loadFirmwareParts({
        manifest: source,
        manifestUrl: new URL('https://example.test/2/installer-manifest.json'),
        mode: 'preservingUpdate',
        fetchFn: vi.fn(async () => ({ ok: true, arrayBuffer: () => new Promise(() => {}) })),
        cryptoApi: webcrypto,
      })
      const rejection = expect(pending).rejects.toMatchObject({ code: 'download-failed' })
      await vi.advanceTimersByTimeAsync(FIRMWARE_DOWNLOAD_TIMEOUT_MS)
      await rejection
    } finally {
      vi.useRealTimers()
    }
  })
})
