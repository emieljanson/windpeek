import { describe, expect, it, vi } from 'vitest'
import { BOARD_IDS } from '../../src/config/configuration'
import { createInstallerSession } from '../../src/installer/createInstallerSession'
import { InstallerError, INSTALLER_ERROR_CODES } from '../../src/installer/installerErrors'
import { configuration, release, appProtocol } from './sessionFixtures'

describe('forecast verification after a firmware update', () => {
  it.each([
    ['same settings, boot render unconfirmed', 'wanted', false],
    ['different settings, boot render unconfirmed', 'old', false],
    ['same settings, boot render confirmed', 'wanted', true],
  ])('actively verifies the forecast instead of waiting for boot recovery: %s', async (_case, digest, bootRendered) => {
    const port = {}
    let flashed = false
    let applied = false
    const oldProtocol = appProtocol({ firmwareVersion: '1.0.0' })
    const newProtocol = appProtocol({ digest })
    const original = newProtocol.request.getMockImplementation()
    newProtocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'get_state') return {
        configurationDigest: applied ? 'wanted' : digest,
        wifi: 'connected', wifiConfigured: true,
        render: bootRendered || applied ? 'valid' : 'pending',
        apply: applied ? 'complete' : 'idle',
      }
      if (command === 'apply_configuration') applied = true
      return original(command, values, timeout)
    })
    const session = createInstallerSession({
      configuration,
      navigatorApi: { serial: { getPorts: async () => [port] } },
      requestPort: async () => port,
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: false, parts: [{ size: 1 }] }),
      protocolFactory: () => flashed ? newProtocol : oldProtocol,
      esptool: {
        identify: async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} }),
        flash: async () => { flashed = true },
      },
      waitFor: async () => {},
    })
    await session.connect()
    const commands = newProtocol.request.mock.calls.map(([command]) => command)
    expect(commands.filter(command => command === 'apply_configuration')).toHaveLength(1)
    expect(session.getState().phase).toBe('complete')
  })
})


describe('installer recovery boundaries', () => {
  it.each(['timeout', 'state_unavailable'])('cleanly reinstalls a recognized app with unreadable settings: %s', async (failure) => {
    const protocol = appProtocol({ firmwareVersion: 'old' })
    const original = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, ...args) => {
      if (command === 'get_state') {
        if (failure === 'timeout') throw new Error('device stopped responding')
        return { status: 'state_unavailable' }
      }
      return original(command, ...args)
    })
    const partsLoader = vi.fn(async () => ({ eraseFlash: true, parts: [] }))
    const esptool = { identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3' })), flash: vi.fn() }
    const session = createInstallerSession({ configuration, requestPort: async () => ({}),
      releaseLoader: async () => release, protocolFactory: () => protocol, partsLoader, esptool })
    await session.connect()
    expect(partsLoader).toHaveBeenCalledWith(expect.objectContaining({ mode: 'cleanInstall' }))
    expect(esptool.flash).toHaveBeenCalledOnce()
    expect(session.getState()).toMatchObject({ phase: 'reconnect', action: { action: 'reinstall' } })
  })

  it.each([
    ['another model', { boardId: BOARD_IDS.E1003 }, 'incompatible-device'],
    ['old firmware', { firmwareVersion: '1.0.0' }, 'verification-failed'],
    ['unconfirmed hardware', { hardwareModel: 'unknown' }, 'incompatible-device'],
  ])('rejects %s after manually reconnecting', async (_name, properties, code) => {
    const protocol = appProtocol(properties)
    const session = createInstallerSession({ configuration, requestPort: async () => ({}),
      releaseLoader: async () => release, protocolFactory: () => protocol })
    await session.reconnect()
    expect(session.getState().error?.code).toBe(code)
    expect(protocol.request.mock.calls.map(([command]) => command)).not.toContain('begin')
    expect(protocol.request.mock.calls.map(([command]) => command)).not.toContain('stage_configuration')
  })
})


describe('setup transaction recovery', () => {
  it.each([
    ['temporary transport failure', { apply: 'render_failed', transportError: -1 }, 2, 'complete'],
    ['temporary provider failure', { apply: 'render_failed', httpStatus: 503 }, 2, 'complete'],
    ['rate limit', { apply: 'render_failed', httpStatus: 429 }, 1, 'verification-issue'],
    ['invalid forecast', { apply: 'render_failed', parseError: 258 }, 1, 'verification-issue'],
    ['storage failure', { apply: 'commit_failed', httpStatus: 200 }, 1, 'verification-issue'],
  ])('handles %s without treating a cached screen as success', async (_name, failure, expectedApplies, phase) => {
    let applies = 0
    let wifiChecks = 0
    const protocol = appProtocol({ wifiHealthy: false })
    const original = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, ...args) => {
      if (command === 'test_wifi') { wifiChecks++; return { status: 'wifi_ready' } }
      if (command === 'apply_configuration') { applies++; return { status: 'applying' } }
      if (command === 'get_state' && applies) return {
        configurationDigest: 'wanted', wifi: 'connected', render: 'valid',
        ...(applies === 1 ? failure : { apply: 'complete' }),
      }
      return original(command, ...args)
    })
    const session = createInstallerSession({ configuration, requestPort: async () => ({}),
      releaseLoader: async () => release, protocolFactory: () => protocol, waitFor: async () => {} })
    await session.connect()
    await session.submitWifi({ ssid: 'Test network', password: 'test-value' })
    expect(session.getState().phase).toBe(phase)
    expect(applies).toBe(expectedApplies)
    expect(wifiChecks).toBe(expectedApplies)
  })

  it('explains a saved setup whose screen is still unconfirmed', async () => {
    const protocol = appProtocol({ wifiHealthy: false })
    const original = protocol.request.getMockImplementation()
    let wifiReady = false
    protocol.request.mockImplementation(async (command, ...args) => {
      if (command === 'test_wifi') { wifiReady = true; return { status: 'wifi_ready' } }
      if (command === 'apply_configuration') return { status: 'applying' }
      if (command === 'get_state' && wifiReady) return {
        configurationDigest: configuration.digest, wifi: 'connected', render: 'pending', apply: 'complete',
      }
      return original(command, ...args)
    })
    const session = createInstallerSession({ configuration, requestPort: async () => ({}),
      releaseLoader: async () => release, protocolFactory: () => protocol })

    await session.connect()
    await session.submitWifi({ ssid: 'Test network', password: 'test-value' })

    expect(session.getState()).toMatchObject({ phase: 'verification-issue',
      error: { message: expect.stringContaining('screen') } })
  })

  it('waits for an interrupted apply to finish without mutating its candidate', async () => {
    let polls = 0
    const protocol = appProtocol()
    const original = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, ...args) => {
      if (command === 'get_state') return { configurationDigest: 'wanted', wifi: 'connected',
        render: 'valid', apply: ++polls < 3 ? 'applying' : 'complete' }
      if (['begin', 'stage_configuration', 'apply_configuration'].includes(command))
        throw new Error('must not mutate a running apply')
      return original(command, ...args)
    })
    const session = createInstallerSession({ configuration, requestPort: async () => ({}),
      releaseLoader: async () => release, protocolFactory: () => protocol, waitFor: async () => {} })
    await session.reconnect()
    expect(session.getState().phase).toBe('complete')
    expect(polls).toBe(3)
  })

  it.each([
    ['storage failure', { apply: 'commit_failed' }, 0, false],
    ['rate limit', { apply: 'render_failed', httpStatus: 429 }, 0, false],
    ['invalid data', { apply: 'render_failed', parseError: 258 }, 0, false],
    ['temporary network failure', { apply: 'render_failed', transportError: -1 }, 1, true],
    ['persistent network failure', { apply: 'render_failed', transportError: -1 }, 1, false],
  ])('preserves the retry policy when reconnecting to %s', async (_name, failure, expectedRetries, recovers) => {
    let polls = 0
    let retries = 0
    const protocol = appProtocol()
    const original = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, ...args) => {
      if (command === 'apply_configuration') { retries++; return { status: 'applying' } }
      if (command === 'get_state') return {
        configurationDigest: 'wanted', wifi: 'connected', render: 'valid',
        ...(++polls === 1 ? { apply: 'applying' }
          : retries && recovers ? { apply: 'complete' } : failure),
      }
      return original(command, ...args)
    })
    const session = createInstallerSession({ configuration, requestPort: async () => ({}),
      releaseLoader: async () => release, protocolFactory: () => protocol, waitFor: async () => {} })
    await session.reconnect()
    expect(session.getState().phase).toBe(recovers ? 'complete' : 'error')
    expect(retries).toBe(expectedRetries)
    for (const command of ['begin', 'stage_configuration']) {
      expect(protocol.request.mock.calls.filter(([name]) => name === command)).toHaveLength(expectedRetries)
    }
  })

  it('does not stage anything after begin reports a busy transaction', async () => {
    const protocol = appProtocol()
    const original = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, ...args) =>
      command === 'begin' ? { status: 'apply_busy' } : original(command, ...args))
    const session = createInstallerSession({ configuration, requestPort: async () => ({}),
      releaseLoader: async () => release, protocolFactory: () => protocol })
    await session.connect()
    expect(session.getState().phase).toBe('error')
    expect(protocol.request.mock.calls.map(([command]) => command)).not.toContain('stage_configuration')
  })
})


it.each([false, true])('acknowledges verified setup before completion, tolerating a lost final reply: %s', async (lostReply) => {
  const protocol = appProtocol({ completionAck: true })
  const original = protocol.request.getMockImplementation()
  let acknowledged = false
  protocol.request.mockImplementation(async (command, ...args) => {
    if (command === 'finish_setup') {
      acknowledged = true
      if (lostReply) throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'reply lost')
      return { status: 'finished' }
    }
    return original(command, ...args)
  })
  const session = createInstallerSession({ configuration, requestPort: async () => ({}),
    releaseLoader: async () => release, protocolFactory: () => protocol })
  session.subscribe(state => { if (state.phase === 'complete') expect(acknowledged).toBe(true) })
  await session.connect()
  expect(protocol.request).toHaveBeenCalledWith('begin', expect.objectContaining({ completionAck: true }))
  expect(session.getState().phase).toBe('complete')
})

// Stateful end-to-end session fixture: erase actually removes saved Wi-Fi and
// configuration, and no valid frame exists until a successful apply commits it.
describe.each([BOARD_IDS.E1001, BOARD_IDS.E1002, BOARD_IDS.E1003])('clean installation on %s', (boardId) => {
  it.each(['slow setup', 'wrong Wi-Fi', 'temporary forecast failure', 'persistent forecast failure', 'commit failure'])(
    'handles %s without reporting an unverified install as complete', async (fault) => {
    let clock = 1787932800000
    let flashed = false
    let hardwareModel = boardId === BOARD_IDS.E1001 ? 'e1001' : 'e1002'
    let wifi = true
    let digest = 'previous-setup'
    let apply = 'idle'
    let render = 'valid'
    let attempts = 0
    let wifiTests = 0
    let finishCalls = 0
    const targetRelease = { ...release, manifest: { ...release.manifest,
      boardId: boardId === BOARD_IDS.E1003 ? BOARD_IDS.E1003 : BOARD_IDS.E1002 } }
    const port = {}
    const protocol = {
      open: vi.fn(), close: vi.fn(),
      request: vi.fn(async (command, values) => {
        if (command === 'hello') return {
          status: 'ok', boardId: targetRelease.manifest.boardId, chipFamily: 'ESP32-S3',
          firmwareVersion: flashed ? release.manifest.version : '1.0.0', protocolVersion: 1,
          configurationVersion: 5, firmwareLayoutVersion: 1,
          capabilities: ['state', 'wifi', 'configuration', 'render-verification', 'clock-sync', 'completion-ack',
            ...(boardId === BOARD_IDS.E1003 ? [] : ['hardware-profile'])],
          hardwareModel, hardwareProfileRevision: 0,
        }
        if (command === 'set_hardware_profile') {
          hardwareModel = values.hardwareModel
          return { status: 'reboot_required' }
        }
        if (command === 'get_state') return {
          configurationDigest: digest, wifi: wifi ? 'connected' : 'disconnected', wifiConfigured: Boolean(digest),
          apply, render, transportError: apply === 'render_failed' ? -1 : 0,
        }
        if (command === 'begin') return { status: 'ready' }
        if (command === 'stage_configuration') return { status: 'configuration_staged' }
        if (command === 'test_wifi') {
          wifi = !(fault === 'wrong Wi-Fi' && wifiTests++ === 0)
          return { status: wifi ? 'wifi_ready' : 'wifi_rejected' }
        }
        if (command === 'apply_configuration') {
          expect(wifi).toBe(true)
          attempts++
          if (fault === 'persistent forecast failure' || (fault === 'temporary forecast failure' && attempts === 1)) {
            apply = 'render_failed'
            wifi = false // Firmware rolls the uncommitted network back.
          } else if (fault === 'commit failure') {
            apply = 'commit_failed'
          } else {
            digest = 'wanted'
            apply = 'complete'
            render = 'valid'
          }
          return { status: 'applying' }
        }
        if (command === 'finish_setup') { finishCalls++; return { status: 'finished' } }
        throw new Error(`Unexpected command ${command}`)
      }),
    }
    const partsLoader = vi.fn(async ({ mode }) => {
      expect(mode).toBe('cleanInstall')
      return { eraseFlash: true, parts: [{ size: 1 }] }
    })
    const session = createInstallerSession({ configuration: { ...configuration, boardId },
      requestPort: async () => port, navigatorApi: { serial: { getPorts: async () => [port] } },
      releaseLoader: async () => targetRelease, partsLoader, protocolFactory: () => protocol,
      now: () => clock, waitFor: async ms => { clock += ms },
      esptool: {
        identify: async () => ({ chipFamily: 'ESP32-S3' }),
        flash: async ({ bundle }) => {
          expect(bundle.eraseFlash).toBe(true)
          flashed = true; wifi = false; digest = ''; render = 'pending'; hardwareModel = 'unknown'
        },
      },
    })
    const phases = []
    session.subscribe(state => phases.push(state.phase))
    await session.connect()
    expect(session.getState().phase).toBe('wifi')
    expect(phases).not.toContain('complete')
    clock += 5 * 3600 * 1000
    const submittedAt = clock
    await session.submitWifi({ ssid: 'Test network', password: 'test-value' })
    if (fault === 'wrong Wi-Fi') {
      expect(session.getState().phase).toBe('wifi')
      await session.submitWifi({ ssid: 'Test network', password: 'corrected-test-value' })
    }
    expect(protocol.request).toHaveBeenCalledWith('begin', { unixTime: Math.floor(submittedAt / 1000), completionAck: true })
    const failed = ['persistent forecast failure', 'commit failure'].includes(fault)
    expect(session.getState().phase).toBe(failed ? 'verification-issue' : 'complete')
    expect(finishCalls).toBe(failed ? 0 : 1)
    expect(attempts).toBe(fault.includes('forecast failure') ? 2 : 1)
    if (failed) {
      expect(digest).toBe('')
      expect(phases).not.toContain('complete')
    }
  })
})


it('stops a screen-profile reboot loop before staging configuration', async () => {
  let flashed = false
  const port = {}
  const oldProtocol = appProtocol({ firmwareVersion: 'old' })
  const newProtocol = appProtocol({ hardwareModel: 'unknown' })
  const original = newProtocol.request.getMockImplementation()
  newProtocol.request.mockImplementation(async (command, ...args) =>
    command === 'set_hardware_profile' ? { status: 'reboot_required' } : original(command, ...args))
  const session = createInstallerSession({ configuration, requestPort: async () => port,
    navigatorApi: { serial: { getPorts: async () => [port] } },
    releaseLoader: async () => release, partsLoader: async () => ({ eraseFlash: true, parts: [] }),
    protocolFactory: () => flashed ? newProtocol : oldProtocol, waitFor: async () => {},
    esptool: { identify: async () => ({ chipFamily: 'ESP32-S3' }), flash: async () => { flashed = true } },
  })
  await session.connect()
  expect(session.getState()).toMatchObject({ phase: 'error', error: { code: 'verification-failed' } })
  expect(newProtocol.request.mock.calls.filter(([command]) => command === 'set_hardware_profile')).toHaveLength(1)
  expect(newProtocol.request.mock.calls.map(([command]) => command)).not.toContain('stage_configuration')
})

it.each([false, true])('recovers a lost screen selection reply (saved before disconnect: %s)', async saved => {
  let flashed = false
  let modelKnown = false
  let selections = 0
  const port = {}
  const oldProtocol = appProtocol({ firmwareVersion: 'old' })
  const newProtocol = appProtocol({ hardwareModel: 'unknown' })
  const original = newProtocol.request.getMockImplementation()
  newProtocol.request.mockImplementation(async (command, ...args) => {
    if (command === 'hello') return { ...await original(command, ...args),
      hardwareModel: modelKnown ? 'e1002' : 'unknown', hardwareProfileRevision: modelKnown ? 1 : 0 }
    if (command === 'set_hardware_profile') {
      selections++
      modelKnown = selections > 1 || saved
      if (selections === 1) throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'USB disconnected')
      return { status: 'reboot_required' }
    }
    return original(command, ...args)
  })
  const flash = vi.fn(async () => { flashed = true })
  const session = createInstallerSession({ configuration, requestPort: async () => port,
    navigatorApi: { serial: { getPorts: async () => [port] } },
    releaseLoader: async () => release, partsLoader: async () => ({ eraseFlash: true, parts: [] }),
    protocolFactory: () => flashed ? newProtocol : oldProtocol, waitFor: async () => {},
    esptool: { identify: async () => ({ chipFamily: 'ESP32-S3' }), flash },
  })
  await session.connect()
  expect(session.getState().phase).toBe('reconnect')
  await session.reconnect()
  expect(session.getState().phase).toBe('complete')
  expect(selections).toBe(saved ? 1 : 2)
  expect(flash).toHaveBeenCalledOnce()
})

it('stops before erasing when bootloader identity differs from the supported chip', async () => {
  const protocol = appProtocol({ firmwareVersion: 'old' })
  const flash = vi.fn()
  const session = createInstallerSession({ configuration, requestPort: async () => ({}),
    releaseLoader: async () => release, partsLoader: async () => ({ eraseFlash: true, parts: [] }),
    protocolFactory: () => protocol,
    esptool: { identify: async () => ({ chipFamily: 'ESP32' }), flash },
  })
  await session.connect()
  expect(session.getState().error?.code).toBe('incompatible-device')
  expect(flash).not.toHaveBeenCalled()
})

it.each(['complete', 'applying'])('reads final status after the browser was suspended for hours: %s', async (apply) => {
  let clock = 1787932800000
  let started = false
  const protocol = appProtocol()
  const original = protocol.request.getMockImplementation()
  protocol.request.mockImplementation(async (command, ...args) => {
    if (command === 'apply_configuration') { started = true; return { status: 'applying' } }
    if (command === 'get_state' && started) return { apply, configurationDigest: 'wanted', wifi: 'connected', render: 'valid' }
    return original(command, ...args)
  })
  const session = createInstallerSession({ configuration, requestPort: async () => ({}),
    releaseLoader: async () => release, protocolFactory: () => protocol,
    now: () => clock, waitFor: async () => { clock += 5 * 3600 * 1000 },
  })
  await session.connect()
  expect(session.getState().phase).toBe(apply === 'complete' ? 'complete' : 'verification-issue')
  expect(protocol.request.mock.calls.filter(([command]) => command === 'get_state')).toHaveLength(2)
})

it('does not send a retry after cancellation during the recovery pause', async () => {
  let applies = 0
  const protocol = appProtocol()
  const original = protocol.request.getMockImplementation()
  protocol.request.mockImplementation(async (command, ...args) => {
    if (command === 'apply_configuration') { applies++; return { status: 'applying' } }
    if (command === 'get_state' && applies) return { apply: 'render_failed', transportError: -1 }
    return original(command, ...args)
  })
  const session = createInstallerSession({ configuration, requestPort: async () => ({}),
    releaseLoader: async () => release, protocolFactory: () => protocol,
    waitFor: async ms => { if (ms === 3000) await session.cancel() },
  })
  await session.connect()
  expect(session.getState().phase).toBe('ready')
  expect(applies).toBe(1)
})


describe('automatic read-only verification recovery', () => {
  it.each(['get_state', 'apply_configuration'])('recovers a lost %s response without another chooser or apply', async (failedCommand) => {
    let applied = false
    const first = appProtocol({ wifiHealthy: false })
    const original = first.request.getMockImplementation()
    first.request.mockImplementation(async (command, ...args) => {
      if (command === 'apply_configuration') applied = true
      if (applied && command === failedCommand) throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'USB read failed')
      return original(command, ...args)
    })
    const recovered = appProtocol()
    const recoveredRequest = recovered.request.getMockImplementation()
    recovered.request.mockImplementation(async (command, ...args) => command === 'get_state'
      ? { configurationDigest: 'wanted', wifi: 'connected', render: 'valid', apply: 'complete' }
      : recoveredRequest(command, ...args))
    const protocolFactory = vi.fn().mockReturnValueOnce(first).mockReturnValue(recovered)
    const requestPort = vi.fn(async () => ({}))
    const reporter = { report: vi.fn(async () => ({ status: 'failed' })) }
    const session = createInstallerSession({ configuration, requestPort, releaseLoader: async () => release,
      protocolFactory, waitFor: async () => {}, reporter })
    await session.connect()
    await session.submitWifi({ ssid: 'Example', password: 'test-only' })
    expect(session.getState().phase).toBe('complete')
    expect(requestPort).toHaveBeenCalledOnce()
    expect(recovered.open).toHaveBeenCalledWith({ resetDevice: false })
    expect(reporter.report).toHaveBeenCalledOnce()
    const snapshot = reporter.report.mock.calls[0][0].snapshot
    expect(snapshot.entries.length).toBeGreaterThan(0)
    expect(JSON.stringify(snapshot)).not.toMatch(/Example|test-only/)
    expect(recovered.request.mock.calls.map(([command]) => command)).not.toContain('begin')
    expect(recovered.request.mock.calls.map(([command]) => command)).not.toContain('apply_configuration')
    expect(first.request.mock.calls.filter(([command]) => command === 'apply_configuration')).toHaveLength(1)
  })
})


it('stops after one automatic connection recovery and never accepts a cached forecast', async () => {
  let applied = false
  const first = appProtocol({ wifiHealthy: false })
  const original = first.request.getMockImplementation()
  first.request.mockImplementation(async (command, ...args) => {
    if (command === 'apply_configuration') applied = true
    if (applied && command === 'get_state') throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'Lost')
    return original(command, ...args)
  })
  const recovered = appProtocol()
  const next = recovered.request.getMockImplementation()
  recovered.request.mockImplementation(async (command, ...args) => {
    if (command === 'get_state') throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'Still lost')
    return next(command, ...args)
  })
  const factory = vi.fn().mockReturnValueOnce(first).mockReturnValue(recovered)
  const session = createInstallerSession({ configuration, requestPort: async () => ({}), releaseLoader: async () => release,
    protocolFactory: factory, waitFor: async () => {} })
  await session.connect()
  await session.submitWifi({ ssid: 'Example', password: 'test-only' })
  expect(factory).toHaveBeenCalledTimes(2)
  expect(session.getState().canRetrySetup).toBe(false)
  expect(session.getState().phase).toBe('verification-issue')
  expect(recovered.request.mock.calls.map(([command]) => command)).not.toContain('apply_configuration')
})

it('retries a terminal forecast failure on the existing connection', async () => {
  const protocol = appProtocol({ digest: 'old' })
  const original = protocol.request.getMockImplementation()
  let applies = 0
  protocol.request.mockImplementation(async (command, ...args) => {
    if (command === 'apply_configuration') { applies++; return { status: 'applying' } }
    if (command === 'get_state' && applies) return { configurationDigest: 'wanted', wifi: 'connected',
      render: 'valid', apply: applies === 1 ? 'render_failed' : 'complete', httpStatus: 429 }
    return original(command, ...args)
  })
  const requestPort = vi.fn(async () => ({}))
  const session = createInstallerSession({ configuration, requestPort, releaseLoader: async () => release,
    protocolFactory: () => protocol, waitFor: async () => {} })
  await session.connect()
  expect(session.getState()).toMatchObject({ phase: 'verification-issue', canRetrySetup: true })
  await session.retrySetup()
  expect(session.getState().phase).toBe('complete')
  expect(requestPort).toHaveBeenCalledOnce()
  expect(applies).toBe(2)
})

it('keeps an unreadable retry status out of the Wi-Fi form', async () => {
  const protocol = appProtocol({ digest: 'old' })
  const original = protocol.request.getMockImplementation()
  let applied = false
  let retrying = false
  protocol.request.mockImplementation(async (command, ...args) => {
    if (command === 'apply_configuration') { applied = true; return { status: 'applying' } }
    if (command === 'get_state' && retrying) return {}
    if (command === 'get_state' && applied) return { wifi: 'connected', apply: 'render_failed', httpStatus: 429 }
    return original(command, ...args)
  })
  const session = createInstallerSession({ configuration, requestPort: async () => ({}), releaseLoader: async () => release,
    protocolFactory: () => protocol, waitFor: async () => {} })
  await session.connect()
  retrying = true
  await session.retrySetup()
  expect(session.getState()).toMatchObject({ phase: 'verification-issue', canRetrySetup: false,
    error: { code: INSTALLER_ERROR_CODES.INVALID_RESPONSE } })
  expect(protocol.request.mock.calls.filter(([command]) => command === 'apply_configuration')).toHaveLength(1)
})
