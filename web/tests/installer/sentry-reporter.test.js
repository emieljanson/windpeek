import { afterEach, describe, expect, it, vi } from 'vitest'
import { createSentryReporter, filterInstallerEvent } from '../../src/installer/sentryReporter'

function fakeSdk({ statusCode = 200, sendError, flushResult = true } = {}) {
  let options
  let transport
  const sdk = {
    init: vi.fn((nextOptions) => {
      options = nextOptions
      transport = options.transport({ url: 'https://example.test/envelope' })
    }),
    makeFetchTransport: vi.fn(() => ({
      async send() {
        if (sendError) throw sendError
        return { statusCode, headers: {} }
      },
      flush: vi.fn().mockResolvedValue(flushResult),
    })),
    captureException: vi.fn((error, context) => {
      const eventId = 'a'.repeat(32)
      const event = options.beforeSend({
        event_id: eventId,
        timestamp: 123,
        level: 'error',
        exception: { values: [{ type: error.name, value: error.message, stacktrace: { frames: [{ filename: 'https://windpeek.test/assets/app.js?secret=yes', function: 'run', lineno: 10, vars: { password: 'secret' }, context_line: 'password=secret' }] } }] },
        tags: context.tags,
        contexts: context.contexts,
        extra: context.extra,
        user: { ip_address: '127.0.0.1' },
        request: { cookies: { session: 'secret' } },
      })
      if (event) void Promise.resolve(transport.send([{ event_id: eventId }, [[{ type: 'event' }, event]]])).catch(() => {})
      return eventId
    }),
    flush: vi.fn().mockImplementation(async () => transport.flush()),
  }
  return sdk
}

function reportInput(overrides = {}) {
  return {
    attempt: 1,
    occurrence: '1:1',
    phase: 'installing-firmware',
    error: Object.assign(new Error('Firmware installation stopped'), { code: 'flash-failed' }),
    snapshot: {
      context: {
        phase: 'installing-firmware',
        errorCode: 'flash-failed',
        action: 'install',
        release: 'v1.2.3',
        boardId: 'seeedstudio_reterminal_e1002',
        selectedBoardId: 'seeedstudio_reterminal_e1003',
        detectedBoardId: 'seeedstudio_reterminal_e1002',
        detectedFirmwareVersion: '2.0.0',
        releaseBoardId: 'seeedstudio_reterminal_e1003',
        releaseVersion: 'v1.2.3',
        connectionKind: 'windpeek',
        decisionReason: 'different-windpeek-model',
        chipFamily: 'ESP32-S3',
      },
      entries: [{ offsetMs: 20, category: 'flash', operation: 'write', status: 'failed', message: 'connection lost', measurements: { writtenBytes: 10 } }],
      textBytes: 15,
    },
    ...overrides,
  }
}

describe('Sentry installer reporter', () => {
  afterEach(() => {
    vi.unstubAllEnvs()
    vi.unstubAllGlobals()
  })

  it.each(['localhost', '127.0.0.1', '127.0.0.2', '[::1]'])('does not report production-build previews on %s', async (hostname) => {
    vi.stubEnv('PROD', true)
    vi.stubGlobal('location', { hostname })
    const sdk = fakeSdk()
    const loadSentry = vi.fn(async () => sdk)
    const reporter = createSentryReporter({ dsn: 'https://public@example.test/1', loadSentry })

    await expect(reporter.report(reportInput())).resolves.toEqual({ status: 'failed' })
    expect(loadSentry).not.toHaveBeenCalled()
  })

  it('does not report automated browser failures on a deployed production build', async () => {
    vi.stubEnv('PROD', true)
    vi.stubGlobal('location', { hostname: 'windpeek.com' })
    vi.stubGlobal('navigator', { webdriver: true })
    const loadSentry = vi.fn(async () => fakeSdk())
    const reporter = createSentryReporter({ dsn: 'https://public@example.test/1', loadSentry })

    await expect(reporter.report(reportInput())).resolves.toEqual({ status: 'failed' })
    expect(loadSentry).not.toHaveBeenCalled()
  })

  it('still reports real production browser failures by default', async () => {
    vi.stubEnv('PROD', true)
    vi.stubGlobal('location', { hostname: 'windpeek.com' })
    vi.stubGlobal('navigator', { webdriver: false })
    const sdk = fakeSdk()
    const reporter = createSentryReporter({ dsn: 'https://public@example.test/1', loadSentry: async () => sdk })

    await expect(reporter.report(reportInput())).resolves.toMatchObject({ status: 'sent' })
    expect(sdk.captureException).toHaveBeenCalledOnce()
  })

  it('does not load Sentry when reporting is disabled or the DSN is absent', async () => {
    const loadSentry = vi.fn()
    const disabled = createSentryReporter({ enabled: false, dsn: 'https://public@example.test/1', loadSentry })
    const missing = createSentryReporter({ enabled: true, dsn: '', loadSentry })

    await expect(disabled.report(reportInput())).resolves.toEqual({ status: 'failed' })
    await expect(missing.report(reportInput())).resolves.toEqual({ status: 'failed' })
    expect(loadSentry).not.toHaveBeenCalled()
  })

  it('initializes with every automatic collection surface disabled', async () => {
    const sdk = fakeSdk()
    const reporter = createSentryReporter({
      enabled: true,
      dsn: 'https://public@example.test/1',
      release: 'build-123',
      loadSentry: async () => sdk,
      randomBytes: () => new Uint8Array([1, 2, 3, 4, 5, 6, 7]),
    })

    await expect(reporter.report(reportInput())).resolves.toEqual({ status: 'sent', reference: expect.stringMatching(/^WS-[0-9A-HJKMNP-TV-Z]{10}$/) })
    expect(sdk.init).toHaveBeenCalledWith(expect.objectContaining({
      defaultIntegrations: false,
      sendClientReports: false,
      enableLogs: false,
      enableMetrics: false,
      tracePropagationTargets: [],
      dataCollection: {
        userInfo: false,
        cookies: false,
        httpHeaders: { request: false, response: false },
        httpBodies: [],
        urlQueryParams: false,
        graphQL: { document: false, variables: false },
        genAI: { inputs: false, outputs: false },
        databaseQueryData: false,
        stackFrameVariables: false,
        frameContextLines: 0,
      },
    }))
    expect(sdk.captureException).toHaveBeenCalledWith(expect.any(Error), expect.objectContaining({
      tags: expect.objectContaining({
        selected_board_id: 'seeedstudio_reterminal_e1003',
        detected_board_id: 'seeedstudio_reterminal_e1002',
        release_board_id: 'seeedstudio_reterminal_e1003',
        connection_kind: 'windpeek',
        decision_reason: 'different-windpeek-model',
      }),
    }))
  })

  it('keeps the real SDK envelope inside the final allowlist', async () => {
    const sdk = await import('@sentry/browser')
    let envelope
    const reporter = createSentryReporter({
      enabled: true,
      dsn: 'https://public@example.test/1',
      release: 'build-real-sdk',
      loadSentry: async () => ({
        ...sdk,
        makeFetchTransport: () => ({
          async send(nextEnvelope) { envelope = nextEnvelope; return { statusCode: 200, headers: {} } },
          async flush() { return true },
        }),
      }),
      randomBytes: () => new Uint8Array([1, 2, 3, 4, 5, 6, 7]),
    })
    sdk.setUser({ email: 'planted@example.test', ip_address: '2001:db8::1' })

    const input = reportInput({ occurrence: 'real-sdk' })
    input.snapshot.entries[0].deviceState = {
      wifi: 'connected', wifiConfigured: true, render: 'pending', apply: 'applying', applyError: 0,
      ssid: 'private-network', configurationDigest: 'private-digest',
    }
    await expect(reporter.report(input))
      .resolves.toEqual({ status: 'sent', reference: expect.stringMatching(/^WS-/) })

    const serialized = JSON.stringify(envelope)
    expect(serialized).toContain('windpeek.reference')
    expect(envelope[1][0][1].extra.timeline[0].measurements).toEqual({ writtenBytes: 10 })
    expect(envelope[1][0][1].extra.timeline[0].deviceState).toEqual({ wifi: 'connected', wifiConfigured: true, render: 'pending', apply: 'applying', applyError: 0 })
    expect(serialized).not.toMatch(/private-network|private-digest/)
    expect(serialized).not.toMatch(/planted@example\.test|2001:db8::1|request|cookies|user_agent/)
    await sdk.close()
  })

  it.each([
    ['non-2xx response', { statusCode: 429 }],
    ['transport rejection', { sendError: new Error('blocked') }],
  ])('returns failed for %s without exposing a reference', async (_, sdkOptions) => {
    const reporter = createSentryReporter({
      enabled: true,
      dsn: 'https://public@example.test/1',
      loadSentry: async () => fakeSdk(sdkOptions),
      randomBytes: () => new Uint8Array(7),
    })

    await expect(reporter.report(reportInput())).resolves.toEqual({ status: 'failed' })
  })

  it('times out a delivery that never reaches the transport', async () => {
    const sdk = fakeSdk()
    sdk.captureException.mockImplementation(() => 'b'.repeat(32))
    const reporter = createSentryReporter({
      enabled: true,
      dsn: 'https://public@example.test/1',
      timeoutMs: 5,
      loadSentry: async () => sdk,
      randomBytes: () => new Uint8Array(7),
    })

    await expect(reporter.report(reportInput())).resolves.toEqual({ status: 'failed' })
  })

  it('deduplicates the same failure occurrence but allows a retry occurrence', async () => {
    const sdk = fakeSdk()
    const reporter = createSentryReporter({
      enabled: true,
      dsn: 'https://public@example.test/1',
      loadSentry: async () => sdk,
      randomBytes: () => new Uint8Array(7),
    })

    await Promise.all([reporter.report(reportInput()), reporter.report(reportInput())])
    await reporter.report(reportInput({ occurrence: '1:2' }))
    expect(sdk.captureException).toHaveBeenCalledTimes(2)
  })
})

describe('filterInstallerEvent', () => {
  it('drops unmarked events and rebuilds marked events from the allowlist', () => {
    expect(filterInstallerEvent({ tags: {} })).toBeNull()

    const filtered = filterInstallerEvent({
      event_id: 'a'.repeat(32),
      timestamp: 123,
      tags: {
        'windpeek.diagnostic': 'installer',
        'windpeek.reference': 'WS-0123456789',
        phase: 'wifi',
        password: 'secret',
      },
      contexts: { installer: { boardId: 'safe', selectedBoardId: 'selected', detectedBoardId: 'detected', decisionReason: 'mismatch', password: 'secret' }, trace: { trace_id: 'private' } },
      extra: { timeline: [{ category: 'wifi', operation: 'test', message: 'safe', password: 'secret' }], password: 'secret' },
      exception: { values: [{ type: 'Error', value: 'safe', stacktrace: { frames: [{ filename: 'https://x.test/app.js?secret=yes', function: 'run', lineno: 1, vars: { password: 'secret' }, context_line: 'secret' }] } }] },
      request: { data: 'secret' },
      user: { email: 'secret@example.test' },
    })

    const encoded = JSON.stringify(filtered)
    expect(encoded).not.toMatch(/password|secret@example|trace_id|context_line|vars|secret=yes/)
    expect(filtered).toMatchObject({
      tags: { 'windpeek.diagnostic': 'installer', 'windpeek.reference': 'WS-0123456789', phase: 'wifi' },
      contexts: { installer: { boardId: 'safe', selectedBoardId: 'selected', detectedBoardId: 'detected', decisionReason: 'mismatch' } },
      extra: { timeline: [{ category: 'wifi', operation: 'test', message: 'safe' }] },
    })
  })
})
