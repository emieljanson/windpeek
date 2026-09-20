import { describe, expect, it } from 'vitest'
import { createDeviceConsoleDecoder, filterDeviceEvidence } from '../../src/installer/deviceEvidence'
import { createInstallerDiagnostics } from '../../src/installer/installerDiagnostics'

describe('device crash evidence', () => {
  it('recognizes the reset line emitted by the physical ESP32-S3 ROM', () => {
    const evidence = []
    const decoder = createDeviceConsoleDecoder((item) => evidence.push(item))
    decoder.feed(new TextEncoder().encode('rst:0xc (RTC_SW_CPU_RST),boot:0x28 (SPI_FAST_FLASH_BOOT)\n'))
    expect(evidence).toEqual([{ kind: 'reset', reset: 12 }])
  })
  it('ignores arbitrary logs, oversized lines and private register/stack data', () => {
    const evidence = []
    const decoder = createDeviceConsoleDecoder((item) => evidence.push(item))
    const text = 'ssid=private-network password=private-password\n' +
      'A2      : 0x3fca1234  A3      : 0xdeadbeef\n' +
      'x'.repeat(1100) + 'Backtrace: 0x42001234:0x3fca0000\n' +
      'Guru Meditation Error: Core  0 panic\'ed (private-reason).\n' +
      'Backtrace: 0x42001234:0x3fca0000 0xdeadbeef:0x3fca0040\n'
    for (const byte of new TextEncoder().encode(text)) decoder.feed([byte])
    decoder.flush()
    expect(evidence).toEqual([{ kind: 'panic' }, { kind: 'backtrace', addresses: [0x42001234] }])
  })

  it('bounds and revalidates evidence at the outgoing boundary', () => {
    expect(filterDeviceEvidence([{ kind: 'backtrace', addresses: [0x42001234, -1, 'secret', 0x3fca0000], password: 'secret' },
      { kind: 'checkpoint', heap: NaN, min: -1, stage: 4, reset: 3, unknown: 'secret' },
      { kind: 'elf', elf: 'secret' }, { kind: 'private-kind' }])).toEqual([
      { kind: 'backtrace', addresses: [0x42001234] }, { kind: 'checkpoint', stage: 4, reset: 3 }, { kind: 'elf' },
    ])
    expect(filterDeviceEvidence(Array.from({ length: 100 }, () => ({ kind: 'reset', reset: 3 })))).toHaveLength(24)
  })

  it('destroys retained crash evidence when the installer session ends', () => {
    const diagnostics = createInstallerDiagnostics()
    diagnostics.recordDeviceEvidence({ kind: 'panic', reason: 'LoadProhibited' })
    expect(diagnostics.snapshot().deviceEvidence).toHaveLength(1)
    diagnostics.destroy()
    diagnostics.recordDeviceEvidence({ kind: 'reset', reset: 3 })
    expect(diagnostics.snapshot()).not.toHaveProperty('deviceEvidence')
  })

  it('keeps crash evidence when subsequent state polling updates the checkpoint', () => {
    const diagnostics = createInstallerDiagnostics()
    diagnostics.recordDeviceEvidence({ kind: 'panic', reason: 'LoadProhibited' })
    for (let i = 0; i < 100; i++) diagnostics.recordDeviceEvidence({ kind: 'checkpoint', stage: i, uptimeMs: i * 1000 })
    expect(diagnostics.snapshot().deviceEvidence).toEqual([
      expect.objectContaining({ kind: 'panic', reason: 'LoadProhibited' }),
      expect.objectContaining({ kind: 'checkpoint', stage: 99, uptimeMs: 99000 }),
    ])
  })
})
