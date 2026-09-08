import { describe, expect, it } from 'vitest'
import { createInstallerDiagnostics, sanitizeDiagnosticText } from '../../src/installer/installerDiagnostics'

describe('installer diagnostics', () => {
  it('keeps a bounded in-memory timeline and evicts the oldest text', () => {
    let time = 1000
    const diagnostics = createInstallerDiagnostics({
      now: () => time += 10,
      maxEntries: 3,
      maxTextBytes: 12,
      maxTextEntryChars: 8,
    })

    diagnostics.record({ category: 'phase', operation: 'one', message: '12345678' })
    diagnostics.record({ category: 'phase', operation: 'two', message: 'abcdef' })
    diagnostics.record({ category: 'phase', operation: 'three', message: 'xyz' })
    diagnostics.record({ category: 'phase', operation: 'four', message: 'last' })

    const snapshot = diagnostics.snapshot()
    expect(snapshot.entries.map((entry) => entry.operation)).toEqual(['three', 'four'])
    expect(snapshot.entries.every((entry) => entry.message.length <= 8)).toBe(true)
    expect(snapshot.textBytes).toBeLessThanOrEqual(12)
  })

  it('removes registered and commonly formatted secrets from nested diagnostic input', () => {
    const diagnostics = createInstallerDiagnostics({ now: () => 1000 })
    diagnostics.registerSensitiveValues({
      ssid: 'Studio Network',
      password: 'correct horse battery staple',
      location: { latitude: 52.0907, longitude: 5.1214 },
      configuration: 'private-setting',
    })
    diagnostics.setContext({
      phase: 'wifi',
      boardId: 'seeedstudio_reterminal_e1002',
      selectedBoardId: 'seeedstudio_reterminal_e1003',
      detectedBoardId: 'seeedstudio_reterminal_e1002',
      decisionReason: 'different-windpeek-model',
      configurationDigest: 'must-not-survive',
      arbitrary: 'also-drop-me',
    })
    diagnostics.record({
      category: 'device',
      operation: 'test_wifi',
      status: 'failed',
      message: 'ssid=Studio Network password=correct horse battery staple email=a@b.test ip=192.168.1.2 lat=52.0907 longitude=5.1214 https://example.test/path?token=secret private-setting',
      measurements: { elapsedMs: 20, password: 123, unknown: 99 },
      payload: { password: 'correct horse battery staple' },
    })

    const encoded = JSON.stringify(diagnostics.snapshot())
    for (const forbidden of [
      'Studio Network', 'correct horse battery staple', '52.0907', '5.1214',
      'private-setting', 'must-not-survive', 'also-drop-me', 'a@b.test',
      '192.168.1.2', 'token=secret',
    ]) expect(encoded).not.toContain(forbidden)
    expect(diagnostics.snapshot()).toMatchObject({
      context: {
        phase: 'wifi',
        boardId: 'seeedstudio_reterminal_e1002',
        selectedBoardId: 'seeedstudio_reterminal_e1003',
        detectedBoardId: 'seeedstudio_reterminal_e1002',
        decisionReason: 'different-windpeek-model',
      },
      entries: [{ category: 'device', operation: 'test_wifi', status: 'failed', measurements: { elapsedMs: 20 } }],
    })
  })

  it('handles cyclic, binary, throwing, and oversized input without escaping its schema', () => {
    const diagnostics = createInstallerDiagnostics({ now: () => 1000, maxTextEntryChars: 16 })
    const cyclic = { safe: 'value' }
    cyclic.self = cyclic
    Object.defineProperty(cyclic, 'boom', { enumerable: true, get() { throw new Error('getter ran') } })

    expect(() => diagnostics.registerSensitiveValues(cyclic)).not.toThrow()
    expect(() => diagnostics.record({
      category: 'serial',
      operation: 'read',
      message: 'x'.repeat(100),
      unknown: cyclic,
      binary: new Uint8Array([1, 2, 3]),
    })).not.toThrow()

    const snapshot = diagnostics.snapshot()
    expect(snapshot.entries[0].message).toHaveLength(16)
    expect(snapshot.entries[0]).not.toHaveProperty('unknown')
    expect(snapshot.entries[0]).not.toHaveProperty('binary')
  })

  it('does not treat ordinary numbers and booleans as secrets', () => {
    const diagnostics = createInstallerDiagnostics({ now: () => 1000 })
    diagnostics.registerSensitiveValues({
      privateName: 'Brouwersdam',
      configurationVersion: 3,
      enabled: true,
    })
    diagnostics.record({
      category: 'device',
      operation: 'version-check',
      message: 'Brouwersdam uses configuration version 3 and enabled=true',
    })

    const encoded = JSON.stringify(diagnostics.snapshot())
    expect(encoded).not.toContain('Brouwersdam')
    expect(encoded).toContain('version 3')
    expect(encoded).toContain('enabled=true')
  })

  it('tracks the credential lock separately from the redaction registry', () => {
    const diagnostics = createInstallerDiagnostics({ now: () => 1000 })
    diagnostics.registerSensitiveValues({ setting: 'redact-me' })
    expect(diagnostics.credentialsLocked).toBe(false)

    const release = diagnostics.acquireCredentialLock({ ssid: 'Home', password: 'secret' })
    expect(diagnostics.credentialsLocked).toBe(true)
    expect(diagnostics.snapshot()).toBeNull()

    release()
    expect(diagnostics.credentialsLocked).toBe(false)
    expect(JSON.stringify(diagnostics.snapshot())).not.toContain('redact-me')
  })

  it('drops free-form text while credentials are live', () => {
    const diagnostics = createInstallerDiagnostics({ now: () => 1000 })
    const release = diagnostics.acquireCredentialLock({ ssid: 'Home', password: 'split-secret' })

    diagnostics.record({ category: 'serial', operation: 'uart', status: 'output', message: 'split-' })
    diagnostics.record({ category: 'serial', operation: 'uart', status: 'output', message: 'secret' })
    release()

    const snapshot = diagnostics.snapshot()
    expect(snapshot.entries).toHaveLength(2)
    expect(snapshot.entries.every((entry) => !('message' in entry))).toBe(true)
    expect(JSON.stringify(snapshot)).not.toMatch(/split|secret/)
  })

  it('destroys all attempt data and cannot be revived', () => {
    const diagnostics = createInstallerDiagnostics({ now: () => 1000 })
    diagnostics.registerSensitiveValues('secret')
    diagnostics.record({ category: 'phase', operation: 'checking', message: 'secret' })
    diagnostics.destroy()
    diagnostics.record({ category: 'phase', operation: 'revived', message: 'later' })

    expect(diagnostics.snapshot()).toEqual({ context: {}, entries: [], textBytes: 0 })
  })
})

describe('sanitizeDiagnosticText', () => {
  it('redacts authorization, email, IP, MAC, coordinates, and query values', () => {
    const value = sanitizeDiagnosticText(
      'Authorization: Bearer abc.def email me@example.com at 10.0.0.1 or 2001:db8::1 or ::1 device aa:bb:cc:dd:ee:ff latitude=51.1 longitude=4.2 https://x.test/a?q=secret',
      [],
    )

    for (const forbidden of ['abc.def', 'me@example.com', '10.0.0.1', '2001:db8::1', '::1', 'aa:bb:cc:dd:ee:ff', '51.1', '4.2', 'q=secret']) {
      expect(value).not.toContain(forbidden)
    }
    expect(sanitizeDiagnosticText('finished at 12:30')).toContain('12:30')
  })
})
