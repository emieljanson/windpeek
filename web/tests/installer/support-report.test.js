import { describe, expect, it, vi } from 'vitest'
import { readSupportReport, sanitizeSupportReport } from '../../src/installer/supportReport'
import { createInstallerDiagnostics } from '../../src/installer/installerDiagnostics'

const metadata = { status: 'ok', deviceId: '0123456789abcdef0123456789abcdef', oldestSequence: 1, newestSequence: 2,
  configuration: { spot: { name: 'Falmouth' }, password: 'secret' } }

describe('device support history', () => {
  it('reads a fixed range and keeps evidence when an entry has been overwritten', async () => {
    const diagnostics = createInstallerDiagnostics()
    const protocol = { request: vi.fn().mockResolvedValueOnce(metadata)
      .mockResolvedValueOnce({ status: 'missing' })
      .mockResolvedValueOnce({ status: 'ok', entry: { sequence: 2, kind: 'setup-failed', result: 258, password: 'secret' } }) }
    await readSupportReport(protocol, diagnostics)
    expect(protocol.request).toHaveBeenCalledTimes(3)
    expect(diagnostics.snapshot().support).toMatchObject({ historyStatus: 'partial', deviceId: metadata.deviceId,
      installedConfiguration: { spot: { name: 'Falmouth' } }, history: [{ sequence: 2, result: 258 }] })
    expect(JSON.stringify(diagnostics.snapshot())).not.toContain('secret')
  })

  it.each([[0, 10], [1, 33], [-1, 0], [1.5, 2], [1, 0x100000000]])('rejects invalid history bounds %s..%s', async (oldestSequence, newestSequence) => {
    const diagnostics = createInstallerDiagnostics()
    const protocol = { request: vi.fn().mockResolvedValue({ ...metadata, oldestSequence, newestSequence }) }
    await readSupportReport(protocol, diagnostics)
    expect(protocol.request).toHaveBeenCalledOnce()
    expect(diagnostics.snapshot().support.historyStatus).toBe('unavailable')
  })

  it('stops collecting when the session is cancelled', async () => {
    const diagnostics = createInstallerDiagnostics()
    const protocol = { request: vi.fn().mockResolvedValue(metadata) }
    await readSupportReport(protocol, diagnostics, () => false)
    expect(protocol.request).toHaveBeenCalledOnce()
    expect(diagnostics.snapshot().support.deviceId).toBeUndefined()
  })

  it('preserves pre-erase evidence across a new installation identity without merging histories', async () => {
    const diagnostics = createInstallerDiagnostics()
    diagnostics.setSupport({ deviceId: metadata.deviceId, history: [{ sequence: 1, kind: 'setup-failed', result: 258 }] })
    diagnostics.preserveBeforeFirmwareErase()
    diagnostics.setSupport({ deviceId: 'ffffffffffffffffffffffffffffffff', history: [{ sequence: 1, kind: 'boot' }] })
    const support = diagnostics.snapshot().support
    expect(support.history).toEqual([{ sequence: 1, kind: 'boot' }])
    expect(support.beforeFirmwareErase.history).toEqual([{ sequence: 1, result: 258, kind: 'setup-failed' }])
    expect(support.beforeFirmwareErase.deviceId).toBe(metadata.deviceId)
  })

  it('clears device evidence when a newly connected device has no history support', () => {
    const diagnostics = createInstallerDiagnostics()
    diagnostics.setSupport({ deviceId: metadata.deviceId, installedConfiguration: metadata.configuration,
      history: [{ sequence: 1, kind: 'setup-failed' }] })
    diagnostics.startSupportCapture({ firmwareVersion: 'legacy', historyStatus: 'unsupported' })
    expect(diagnostics.snapshot().support).toMatchObject({ firmwareVersion: 'legacy', historyStatus: 'unsupported', history: [] })
    expect(diagnostics.snapshot().support.deviceId).toBeUndefined()
    expect(diagnostics.snapshot().support.installedConfiguration).toBeUndefined()
  })

  it('bounds nested settings and does not retain recursive previous reports', () => {
    const config = { spot: { name: 'A'.repeat(1000) }, password: 'secret' }
    config.additionalSpots = Array(100).fill(config)
    const support = { selectedConfiguration: config, history: Array(100).fill({ kind: 'boot', configuration: config }) }
    support.beforeFirmwareErase = support
    const safe = sanitizeSupportReport(support)
    expect(safe.history).toHaveLength(32)
    expect(safe.selectedConfiguration.additionalSpots).toHaveLength(9)
    expect(safe.selectedConfiguration.additionalSpots[0].additionalSpots).toBeUndefined()
    expect(safe.beforeFirmwareErase.beforeFirmwareErase).toBeUndefined()
    expect(safe.selectedConfiguration.spot.name).toHaveLength(64)
    expect(JSON.stringify(safe)).not.toContain('secret')
  })
})
