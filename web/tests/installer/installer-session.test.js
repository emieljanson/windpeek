import { describe, expect, it, vi } from 'vitest'
import {
  BOARD_IDS,
  CONFIGURATION_VERSION,
  createInstalledConfiguration,
} from '../../src/config/configuration'
import { createInstallerSession } from '../../src/installer/createInstallerSession'
import { createInstallerDiagnostics } from '../../src/installer/installerDiagnostics'
import { InstallerError, INSTALLER_ERROR_CODES } from '../../src/installer/installerErrors'

const configuration = { digest: 'wanted' }
const release = { manifest: { version: '2.0.0', boardId: 'seeedstudio_reterminal_e1002', chipFamily: 'ESP32-S3', firmwareLayoutVersion: 1 }, manifestUrl: new URL('https://example.test/manifest.json') }

function e1001Configuration() {
  return createInstalledConfiguration({
    boardId: BOARD_IDS.E1001,
    spot: {
      id: 'edam', name: 'Edam', latitude: 52.5126, longitude: 5.0486,
      timezone: 'Europe/Amsterdam',
    },
    modelId: 'best_match',
    display: {
      showThreshold: false, threshold: 17, showWeather: true,
      showTemperature: false, showTide: false, showDedicatedFooter: true,
      timeFormat: '24-hour', temperatureUnit: 'celsius',
    },
  })
}

function appProtocol(state = {}) {
  let activeDigest = state.digest ?? 'wanted'
  let stagedDigest = 'wanted'
  return {
    open: vi.fn(), close: vi.fn(), request: vi.fn(async (command, values) => {
      if (command === 'hello') return {
        status: 'ok', boardId: state.boardId ?? release.manifest.boardId, chipFamily: 'ESP32-S3',
        firmwareVersion: state.firmwareVersion ?? '2.0.0', protocolVersion: 1,
        configurationVersion: state.configurationVersion ?? CONFIGURATION_VERSION,
        firmwareLayoutVersion: state.firmwareLayoutVersion,
        capabilities: [
          'state', 'wifi', 'configuration', 'render-verification', 'clock-sync',
          ...(state.hardwareModel ? ['hardware-profile'] : []),
        ],
        ...(state.hardwareModel ? {
          hardwareModel: state.hardwareModel,
          hardwareProfileRevision: state.hardwareProfileRevision ?? 0,
        } : {}),
      }
      if (command === 'get_state') return { configurationDigest: activeDigest, wifi: state.wifiHealthy === false ? 'disconnected' : 'connected', render: 'valid' }
      if (command === 'stage_configuration') {
        stagedDigest = values?.configuration?.digest ?? 'wanted'
        return { status: 'configuration_staged' }
      }
      if (command === 'apply_configuration') { activeDigest = stagedDigest; return { status: 'complete' } }
      return { ok: true }
    }),
  }
}

describe('installer session', () => {
  it('registers only private spot text for diagnostic redaction', () => {
    const diagnostics = { registerSensitiveValues: vi.fn(), setContext: vi.fn() }
    createInstallerSession({
      configuration: {
        version: 3,
        boardId: release.manifest.boardId,
        digest: 'public-digest',
        spot: { id: 'brouwersdam', name: 'Brouwersdam', timezone: 'Europe/Amsterdam' },
      },
      diagnostics,
    })

    expect(diagnostics.registerSensitiveValues).toHaveBeenCalledWith([
      'brouwersdam', 'Brouwersdam', 'Europe/Amsterdam',
    ])
  })

  it('returns to ready when the system chooser is cancelled', async () => {
    const reporter = { report: vi.fn() }
    const session = createInstallerSession({ configuration, requestPort: async () => null, reporter })
    await session.connect()
    expect(session.getState()).toMatchObject({ phase: 'ready', error: null })
    expect(reporter.report).not.toHaveBeenCalled()
  })

  it('turns a device chooser failure into a recoverable installer state', async () => {
    const session = createInstallerSession({
      configuration,
      requestPort: async () => { throw new Error('permission failed') },
    })
    await session.connect()
    expect(session.getState()).toMatchObject({ phase: 'error', safeToDisconnect: true })
  })

  it('reports an already-open serial port instead of calling the E1002 incompatible', async () => {
    const portBusy = Object.assign(new Error('Failed to open serial port'), { name: 'NetworkError' })
    const protocol = { open: vi.fn().mockRejectedValue(portBusy), close: vi.fn() }
    const esptool = { identify: vi.fn() }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
      esptool,
    })

    await session.connect()

    expect(esptool.identify).not.toHaveBeenCalled()
    expect(session.getState()).toMatchObject({
      phase: 'error',
      error: {
        code: INSTALLER_ERROR_CODES.DEVICE_NOT_ALLOWED,
        message: 'This USB device is already in use. Close other Windpeek tabs or serial tools, then try again.',
      },
    })
  })

  it('shows the device selection step while the browser chooser is open', async () => {
    let resolvePort
    const session = createInstallerSession({
      configuration,
      requestPort: () => new Promise((resolve) => { resolvePort = resolve }),
    })

    const connecting = session.connect()
    expect(session.getState().phase).toBe('choosing-device')
    resolvePort(null)
    await connecting
    expect(session.getState().phase).toBe('ready')
  })

  it('releases app mode when loading the firmware release fails', async () => {
    const protocol = appProtocol()
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => { throw new Error('release unavailable') },
      protocolFactory: () => protocol,
    })

    await session.connect()

    expect(protocol.close).toHaveBeenCalledOnce()
    expect(session.getState().phase).toBe('error')
  })

  it('completes immediately when an installed Windpeek is current', async () => {
    const protocol = appProtocol()
    const session = createInstallerSession({ configuration, requestPort: async () => ({}), releaseLoader: async () => release, protocolFactory: () => protocol })
    await session.connect()
    expect(session.getState().action.action).toBe('up-to-date')
    expect(session.getState().phase).toBe('complete')
    expect(protocol.request).not.toHaveBeenCalledWith('stage_configuration', expect.anything())
  })

  it('selects the E1003 release and accepts a matching installed device', async () => {
    const e1003Release = {
      ...release,
      manifest: { ...release.manifest, boardId: BOARD_IDS.E1003 },
    }
    const releaseLoader = vi.fn(async () => e1003Release)
    const session = createInstallerSession({
      configuration: { ...configuration, boardId: BOARD_IDS.E1003 },
      requestPort: async () => ({}),
      releaseLoader,
      protocolFactory: () => appProtocol({ boardId: BOARD_IDS.E1003 }),
    })

    await session.connect()

    expect(releaseLoader).toHaveBeenCalledWith(expect.objectContaining({ boardId: BOARD_IDS.E1003 }))
    expect(session.getState()).toMatchObject({ phase: 'complete', action: { action: 'up-to-date' } })
  })

  it('keeps E1001 hardware identity outside the E1001/E1002 configuration payload', async () => {
    const selectedConfiguration = e1001Configuration()
    const protocol = appProtocol({ hardwareModel: 'e1001', digest: 'old' })
    const session = createInstallerSession({
      configuration: selectedConfiguration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })

    await session.connect()

    const stagedCall = protocol.request.mock.calls.find(([command]) => command === 'stage_configuration')
    expect(stagedCall).toBeDefined()
    const staged = stagedCall[1]
    expect(staged.configuration.boardId).toBe(BOARD_IDS.E1002)
    expect(staged.configuration.digest).not.toBe(selectedConfiguration.digest)
    expect(session.getState().phase).toBe('complete')
  })

  it('asks for confirmation when universal E1002 firmware has no saved hardware profile', async () => {
    const session = createInstallerSession({
      configuration: { ...configuration, boardId: BOARD_IDS.E1002 },
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => appProtocol({ hardwareModel: 'unknown' }),
    })

    await session.connect()

    expect(session.getState().phase).toBe('confirm-device')
  })

  it('blocks a known universal hardware profile that differs from the selected model', async () => {
    const session = createInstallerSession({
      configuration: e1001Configuration(),
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => appProtocol({ hardwareModel: 'e1002' }),
    })

    await session.connect()

    expect(session.getState()).toMatchObject({
      phase: 'error',
      error: { code: INSTALLER_ERROR_CODES.INCOMPATIBLE_DEVICE },
    })
  })

  it('blocks an E1002 when the E1003 installer route is selected', async () => {
    const e1003Release = {
      ...release,
      manifest: { ...release.manifest, boardId: BOARD_IDS.E1003 },
    }
    const session = createInstallerSession({
      configuration: { ...configuration, boardId: BOARD_IDS.E1003 },
      requestPort: async () => ({}),
      releaseLoader: async () => e1003Release,
      protocolFactory: () => appProtocol({ boardId: BOARD_IDS.E1002 }),
    })

    await session.connect()

    expect(session.getState()).toMatchObject({
      phase: 'error',
      error: { code: INSTALLER_ERROR_CODES.INCOMPATIBLE_DEVICE },
    })
  })

  it('reports both device identities when an E1002 is connected on the E1003 route', async () => {
    const e1003Release = {
      ...release,
      manifest: { ...release.manifest, version: '3.0.0', boardId: BOARD_IDS.E1003 },
    }
    const diagnostics = createInstallerDiagnostics({ now: () => 1000 })
    const reporter = { report: vi.fn(async () => ({ status: 'sent', reference: 'WS-TEST123456' })) }
    const session = createInstallerSession({
      configuration: { ...configuration, boardId: BOARD_IDS.E1003 },
      requestPort: async () => ({}),
      releaseLoader: async () => e1003Release,
      protocolFactory: () => appProtocol({ boardId: BOARD_IDS.E1002, firmwareVersion: '2.4.1' }),
      diagnostics,
      reporter,
    })

    await session.connect()
    await vi.waitFor(() => expect(reporter.report).toHaveBeenCalledOnce())

    expect(reporter.report.mock.calls[0][0]).toMatchObject({
      phase: 'checking-device',
      error: { code: INSTALLER_ERROR_CODES.INCOMPATIBLE_DEVICE },
      snapshot: {
        context: {
          selectedBoardId: BOARD_IDS.E1003,
          detectedBoardId: BOARD_IDS.E1002,
          detectedFirmwareVersion: '2.4.1',
          releaseBoardId: BOARD_IDS.E1003,
          releaseVersion: '3.0.0',
          connectionKind: 'windpeek',
          decisionReason: 'different-windpeek-model',
        },
      },
    })
  })

  it('waits for saved Wi-Fi to reconnect after the serial open reset', async () => {
    let stateChecks = 0
    const protocol = appProtocol()
    const originalRequest = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'get_state') {
        stateChecks += 1
        return {
          configurationDigest: 'wanted',
          wifiConfigured: true,
          wifi: stateChecks < 3 ? 'disconnected' : 'connected',
          render: 'valid',
          apply: 'idle',
        }
      }
      return originalRequest(command, values, timeout)
    })
    const waitFor = vi.fn().mockResolvedValue(undefined)
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
      waitFor,
    })

    await session.connect()

    expect(waitFor).toHaveBeenCalledTimes(2)
    expect(session.getState()).toMatchObject({ phase: 'complete', progress: 1 })
    expect(protocol.request).not.toHaveBeenCalledWith('stage_configuration', expect.anything())
  })

  it('updates a changed configuration without loading firmware', async () => {
    const protocol = appProtocol({ digest: 'old' })
    const partsLoader = vi.fn()
    const session = createInstallerSession({ configuration, requestPort: async () => ({}), releaseLoader: async () => release, partsLoader, protocolFactory: () => protocol })
    await session.connect()
    expect(partsLoader).not.toHaveBeenCalled()
    expect(protocol.request).toHaveBeenCalledWith('stage_configuration', { configuration })
    expect(session.getState().phase).toBe('complete')
  })

  it('reuses a recognized granted device when the installer is opened again in the same tab', async () => {
    const selectedPort = { id: 'recognized-port' }
    const serial = { getPorts: vi.fn(async () => [selectedPort]) }
    const navigatorApi = { serial }
    const firstRequestPort = vi.fn(async () => selectedPort)
    const firstSession = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort: firstRequestPort,
      releaseLoader: async () => release,
      protocolFactory: () => appProtocol(),
    })

    await firstSession.connect()
    await firstSession.cancel()

    const secondRequestPort = vi.fn(async () => selectedPort)
    const secondSession = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort: secondRequestPort,
      releaseLoader: async () => release,
      protocolFactory: () => appProtocol(),
    })

    await secondSession.connect()

    expect(firstRequestPort).toHaveBeenCalledOnce()
    expect(serial.getPorts).toHaveBeenCalledOnce()
    expect(secondRequestPort).not.toHaveBeenCalled()
    expect(secondSession.getState().phase).toBe('complete')
  })

  it('does not open the chooser after cancelling a pending remembered-device lookup', async () => {
    const selectedPort = { id: 'recognized-port' }
    let finishLookup
    const serial = { getPorts: vi.fn() }
    const navigatorApi = { serial }
    const firstSession = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort: async () => selectedPort,
      releaseLoader: async () => release,
      protocolFactory: () => appProtocol(),
    })
    await firstSession.connect()
    await firstSession.cancel()

    serial.getPorts.mockImplementation(() => new Promise((resolve) => { finishLookup = resolve }))
    const requestPort = vi.fn(async () => null)
    const secondSession = createInstallerSession({ configuration, navigatorApi, requestPort })
    const connecting = secondSession.connect()
    await vi.waitFor(() => expect(finishLookup).toBeTypeOf('function'))

    await secondSession.cancel()
    finishLookup([])
    await connecting

    expect(requestPort).not.toHaveBeenCalled()
    expect(secondSession.getState().phase).toBe('ready')
  })

  it('falls back to the chooser when the remembered device permission is no longer granted', async () => {
    const selectedPort = { id: 'recognized-port' }
    const serial = { getPorts: vi.fn(async () => []) }
    const navigatorApi = { serial }
    const firstSession = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort: async () => selectedPort,
      releaseLoader: async () => release,
      protocolFactory: () => appProtocol(),
    })
    await firstSession.connect()
    await firstSession.cancel()

    const secondRequestPort = vi.fn(async () => null)
    const secondSession = createInstallerSession({ configuration, navigatorApi, requestPort: secondRequestPort })
    await secondSession.connect()

    expect(serial.getPorts).toHaveBeenCalledOnce()
    expect(secondRequestPort).toHaveBeenCalledOnce()
    expect(secondSession.getState().phase).toBe('ready')
  })

  it('falls back to the chooser when a remembered granted port is stale', async () => {
    const stalePort = { id: 'stale-port' }
    const freshPort = { id: 'fresh-port' }
    const serial = { getPorts: vi.fn(async () => [stalePort]) }
    const navigatorApi = { serial }
    const firstSession = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort: async () => stalePort,
      releaseLoader: async () => release,
      protocolFactory: () => appProtocol(),
    })
    await firstSession.connect()
    await firstSession.cancel()

    const staleProtocol = {
      open: vi.fn().mockRejectedValue(new Error('stale port')),
      close: vi.fn(),
    }
    const requestPort = vi.fn(async () => freshPort)
    const secondSession = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort,
      releaseLoader: async () => release,
      protocolFactory: vi.fn()
        .mockReturnValueOnce(staleProtocol)
        .mockReturnValueOnce(appProtocol()),
      esptool: { identify: vi.fn().mockRejectedValue(new Error('device disconnected')) },
    })

    await secondSession.connect()

    expect(requestPort).toHaveBeenCalledOnce()
    expect(secondSession.getState().phase).toBe('complete')
  })

  it('does not remember an unverified ESP32-S3 as a supported reTerminal', async () => {
    const selectedPort = { id: 'unverified-port' }
    const serial = { getPorts: vi.fn(async () => [selectedPort]) }
    const navigatorApi = { serial }
    const bootloaderProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: { disconnect: vi.fn() } })),
      flash: vi.fn(),
    }
    const firstSession = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort: async () => selectedPort,
      releaseLoader: async () => release,
      protocolFactory: () => bootloaderProtocol,
      esptool,
    })
    await firstSession.connect()
    await firstSession.cancel()

    const secondRequestPort = vi.fn(async () => null)
    const secondSession = createInstallerSession({ configuration, navigatorApi, requestPort: secondRequestPort })
    await secondSession.connect()

    expect(serial.getPorts).not.toHaveBeenCalled()
    expect(secondRequestPort).toHaveBeenCalledOnce()
    expect(secondSession.getState().phase).toBe('ready')
  })

  it('starts a preserving firmware update without a second confirmation', async () => {
    const protocol = appProtocol({ configurationVersion: 2 })
    const partsLoader = vi.fn(async () => ({ eraseFlash: false, parts: [{ size: 1 }] }))
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(),
    }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      partsLoader,
      protocolFactory: () => protocol,
      esptool,
    })

    await session.connect()

    expect(session.getState().action).toEqual({
      action: 'update-firmware', reason: 'configuration-version-outdated',
    })
    expect(partsLoader).toHaveBeenCalledWith(expect.objectContaining({ mode: 'preservingUpdate' }))
    expect(esptool.flash).toHaveBeenCalledOnce()
    expect(session.getState().phase).toBe('reconnect')
  })

  it('repairs a recognized Windpeek without a redundant confirmation', async () => {
    const protocol = appProtocol({ firmwareLayoutVersion: 2 })
    const partsLoader = vi.fn(async () => ({ eraseFlash: true, parts: [] }))
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(),
    }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      partsLoader,
      protocolFactory: () => protocol,
      esptool,
    })

    await session.connect()

    expect(session.getState().action).toEqual({ action: 'reinstall', reason: 'flash-layout-changed' })
    expect(partsLoader).toHaveBeenCalledWith(expect.objectContaining({ mode: 'cleanInstall' }))
    expect(esptool.flash).toHaveBeenCalledOnce()
    expect(session.getState().phase).toBe('reconnect')
  })

  it('sets the device clock from the browser before sending configuration', async () => {
    const protocol = appProtocol({ digest: 'old' })
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
      now: () => 1787932800123,
    })

    await session.connect()

    expect(protocol.request).toHaveBeenCalledWith('begin', { unixTime: 1787932800 })
    const beginOrder = protocol.request.mock.invocationCallOrder[
      protocol.request.mock.calls.findIndex(([command]) => command === 'begin')
    ]
    const stageOrder = protocol.request.mock.invocationCallOrder[
      protocol.request.mock.calls.findIndex(([command]) => command === 'stage_configuration')
    ]
    expect(beginOrder).toBeLessThan(stageOrder)
  })

  it('stops before configuration when the device rejects the browser clock', async () => {
    const protocol = appProtocol({ digest: 'old' })
    const originalRequest = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'begin') return { status: 'clock_rejected' }
      return originalRequest(command, values, timeout)
    })
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })

    await session.connect()

    expect(session.getState()).toMatchObject({ phase: 'error' })
    expect(protocol.request).not.toHaveBeenCalledWith('stage_configuration', expect.anything())
  })

  it('stops before USB setup when the browser clock is unusable', async () => {
    const protocol = appProtocol({ digest: 'old' })
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
      now: () => Number.NaN,
    })

    await session.connect()

    expect(session.getState()).toMatchObject({ phase: 'error' })
    expect(protocol.request).not.toHaveBeenCalledWith('begin', expect.anything())
    expect(protocol.request).not.toHaveBeenCalledWith('stage_configuration', expect.anything())
  })

  it('polls an asynchronous device render instead of timing out the USB request', async () => {
    let stateChecks = 0
    const protocol = appProtocol({ digest: 'old' })
    protocol.request.mockImplementation(async (command) => {
      if (command === 'hello') return {
        status: 'ok', boardId: release.manifest.boardId, chipFamily: 'ESP32-S3',
        firmwareVersion: '2.0.0', protocolVersion: 1, configurationVersion: CONFIGURATION_VERSION,
        capabilities: ['state', 'wifi', 'configuration', 'render-verification', 'clock-sync'],
      }
      if (command === 'get_state') {
        stateChecks += 1
        if (stateChecks === 1) {
          return { configurationDigest: 'old', wifi: 'connected', render: 'pending', apply: 'idle' }
        }
        if (stateChecks < 4) {
          // A previous valid frame and the desired digest must not make an
          // in-progress physical panel refresh look complete.
          return { configurationDigest: 'wanted', wifi: 'connected', render: 'valid', apply: 'applying' }
        }
        return { configurationDigest: 'wanted', wifi: 'connected', render: 'valid', apply: 'complete' }
      }
      if (command === 'stage_configuration') return { status: 'configuration_staged' }
      if (command === 'apply_configuration') return { status: 'applying' }
      return { status: 'ok' }
    })
    const waitFor = vi.fn().mockResolvedValue(undefined)
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
      waitFor,
    })

    await session.connect()

    expect(waitFor).toHaveBeenCalledTimes(3)
    expect(protocol.request).toHaveBeenCalledWith('apply_configuration', undefined, 120_000)
    expect(protocol.request.mock.calls.filter(([command]) => command === 'get_state').slice(1))
      .toEqual(expect.arrayContaining([
        ['get_state', undefined, 120_000],
      ]))
    expect(session.getState()).toMatchObject({ phase: 'complete', progress: 1 })
  })

  it('keeps the browser request alive beyond the device Wi-Fi timeout', async () => {
    const protocol = appProtocol({ wifiHealthy: false })
    const sentCredentials = []
    const originalRequest = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'test_wifi') sentCredentials.push({ ...values })
      return originalRequest(command, values, timeout)
    })
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })

    await session.connect()
    await session.submitWifi({ ssid: 'Home', password: 'secret' })

    expect(sentCredentials).toEqual([{ ssid: 'Home', password: 'secret' }])
    const transmittedReference = protocol.request.mock.calls.find(([command]) => command === 'test_wifi')[1]
    expect(transmittedReference.password).toBe('')
    expect(transmittedReference.ssid).toBe('')
  })

  it('reports a Wi-Fi failure only after credential references are cleared', async () => {
    const protocol = appProtocol({ wifiHealthy: false })
    const originalRequest = protocol.request.getMockImplementation()
    let transmittedReference
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'test_wifi') {
        transmittedReference = values
        throw new InstallerError(INSTALLER_ERROR_CODES.WIFI_FAILED, 'wifi rejected')
      }
      return originalRequest(command, values, timeout)
    })
    const reporter = { report: vi.fn(async () => ({ status: 'sent', reference: 'WS-TEST12345' })) }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
      reporter,
    })

    await session.connect()
    await session.submitWifi({ ssid: 'Home', password: 'secret' })
    await vi.waitFor(() => expect(reporter.report).toHaveBeenCalledOnce())

    expect(transmittedReference).toEqual({ ssid: '', password: '' })
    expect(JSON.stringify(reporter.report.mock.calls[0][0].snapshot)).not.toMatch(/Home|secret/)
    expect(reporter.report.mock.calls[0][0].snapshot.context).toMatchObject({
      release: '2.0.0',
      route: 'update-configuration',
      boardId: release.manifest.boardId,
      chipFamily: 'ESP32-S3',
      layoutVersion: 1,
    })
    expect(reporter.report.mock.calls[0][0].snapshot.entries)
      .toEqual(expect.arrayContaining([
        expect.objectContaining({ category: 'state', operation: 'wifi', status: 'entered' }),
      ]))
  })

  it('keeps diagnostic cleanup failures outside Wi-Fi recovery', async () => {
    const protocol = appProtocol({ wifiHealthy: false })
    const originalRequest = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'test_wifi') throw new InstallerError(INSTALLER_ERROR_CODES.WIFI_FAILED, 'wifi rejected')
      return originalRequest(command, values, timeout)
    })
    const diagnostics = createInstallerDiagnostics()
    const acquireCredentialLock = diagnostics.acquireCredentialLock
    diagnostics.acquireCredentialLock = (credentials) => {
      const releaseLock = acquireCredentialLock(credentials)
      return () => { releaseLock(); throw new Error('cleanup failed') }
    }
    const reporter = { report: vi.fn(async () => ({ status: 'failed' })) }
    const session = createInstallerSession({
      configuration,
      diagnostics,
      reporter,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })

    await session.connect()
    await expect(session.submitWifi({ ssid: 'Home', password: 'secret' })).resolves.toMatchObject({ phase: 'wifi' })
    await vi.waitFor(() => expect(reporter.report).toHaveBeenCalledOnce())
  })

  it('ignores an old Wi-Fi report after a successful retry', async () => {
    let finishReport
    let wifiAttempts = 0
    const protocol = appProtocol({ wifiHealthy: false })
    const originalRequest = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'test_wifi' && wifiAttempts++ === 0) {
        throw new InstallerError(INSTALLER_ERROR_CODES.WIFI_FAILED, 'wifi rejected')
      }
      if (command === 'test_wifi') return { status: 'wifi_ready' }
      if (command === 'get_state' && wifiAttempts >= 2) {
        return { configurationDigest: 'wanted', wifi: 'connected', render: 'valid', apply: 'complete' }
      }
      return originalRequest(command, values, timeout)
    })
    const reporter = { report: vi.fn(() => new Promise((resolve) => { finishReport = resolve })) }
    const session = createInstallerSession({
      configuration,
      reporter,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })

    await session.connect()
    await session.submitWifi({ ssid: 'Home', password: 'wrong' })
    await vi.waitFor(() => expect(session.getState().diagnosticStatus).toBe('sending'))
    await session.submitWifi({ ssid: 'Home', password: 'right' })
    expect(session.getState().phase).toBe('complete')

    finishReport({ status: 'sent', reference: 'WS-OLDREF1234' })
    await Promise.resolve()
    expect(session.getState()).toMatchObject({ phase: 'complete', diagnosticStatus: 'idle', diagnosticReference: null })
  })

  it('reports a failed Wi-Fi scan once while leaving retry available', async () => {
    const protocol = appProtocol({ wifiHealthy: false })
    const originalRequest = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'scan_networks') throw new Error('scan failed')
      return originalRequest(command, values, timeout)
    })
    const reporter = { report: vi.fn(async () => ({ status: 'failed' })) }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
      reporter,
    })

    await session.connect()
    await expect(session.scanNetworks()).rejects.toMatchObject({ code: INSTALLER_ERROR_CODES.WIFI_FAILED })
    await vi.waitFor(() => expect(reporter.report).toHaveBeenCalledOnce())

    expect(session.getState()).toMatchObject({ phase: 'wifi', safeToDisconnect: true })
  })

  it('does not let a late Wi-Fi scan failure reopen a completed setup', async () => {
    let rejectScan
    const scanResult = new Promise((_, reject) => { rejectScan = reject })
    const protocol = appProtocol({ wifiHealthy: false })
    const originalRequest = protocol.request.getMockImplementation()
    let wifiReady = false
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'scan_networks') return scanResult
      if (command === 'test_wifi') {
        wifiReady = true
        return { status: 'wifi_ready' }
      }
      if (command === 'get_state' && wifiReady) {
        return { configurationDigest: 'wanted', wifi: 'connected', render: 'valid', apply: 'complete' }
      }
      return originalRequest(command, values, timeout)
    })
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })

    await session.connect()
    const scanning = session.scanNetworks()
    await session.submitWifi({ ssid: 'Home', password: 'secret' })
    expect(session.getState().phase).toBe('complete')

    rejectScan(new Error('late scan failure'))
    await expect(scanning).rejects.toMatchObject({ code: INSTALLER_ERROR_CODES.WIFI_FAILED })
    expect(session.getState()).toMatchObject({ phase: 'complete', error: null })
  })

  it('releases a timed-out serial session before reopening the device', async () => {
    const timedOutProtocol = appProtocol({ wifiHealthy: false })
    const originalRequest = timedOutProtocol.request.getMockImplementation()
    timedOutProtocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'test_wifi') {
        throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'reader cancelled')
      }
      return originalRequest(command, values, timeout)
    })
    const recoveredProtocol = appProtocol()
    const protocolFactory = vi.fn()
      .mockReturnValueOnce(timedOutProtocol)
      .mockReturnValueOnce(recoveredProtocol)
    const requestPort = vi.fn()
      .mockResolvedValueOnce({ id: 'initial-port' })
      .mockResolvedValueOnce({ id: 'reconnected-port' })
    const session = createInstallerSession({
      configuration,
      requestPort,
      releaseLoader: async () => release,
      protocolFactory,
    })

    await session.connect()
    await session.submitWifi({ ssid: 'Home', password: 'secret' })
    expect(session.getState().phase).toBe('reconnect')

    await session.reconnect()

    expect(timedOutProtocol.close).toHaveBeenCalledBefore(recoveredProtocol.open)
    expect(session.getState().phase).toBe('complete')
  })

  it('resumes verification after reconnect without reapplying an already committed setup', async () => {
    const disconnectedProtocol = appProtocol({ wifiHealthy: false })
    const originalDisconnectedRequest = disconnectedProtocol.request.getMockImplementation()
    disconnectedProtocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'test_wifi') {
        throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'reader cancelled')
      }
      return originalDisconnectedRequest(command, values, timeout)
    })
    let stateChecks = 0
    const recoveredProtocol = appProtocol()
    const originalRecoveredRequest = recoveredProtocol.request.getMockImplementation()
    recoveredProtocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'get_state') {
        stateChecks += 1
        return {
          configurationDigest: 'wanted',
          wifiConfigured: true,
          wifi: 'connected',
          render: stateChecks < 2 ? 'pending' : 'valid',
          apply: 'idle',
        }
      }
      return originalRecoveredRequest(command, values, timeout)
    })
    const protocolFactory = vi.fn()
      .mockReturnValueOnce(disconnectedProtocol)
      .mockReturnValueOnce(recoveredProtocol)
    const requestPort = vi.fn()
      .mockResolvedValueOnce({ id: 'initial-port' })
      .mockResolvedValueOnce({ id: 'reconnected-port' })
    const waitFor = vi.fn().mockResolvedValue(undefined)
    const session = createInstallerSession({
      configuration,
      requestPort,
      releaseLoader: async () => release,
      protocolFactory,
      waitFor,
    })

    await session.connect()
    await session.submitWifi({ ssid: 'Home', password: 'secret' })
    expect(session.getState().phase).toBe('reconnect')

    await session.reconnect()

    expect(session.getState().phase).toBe('complete')
    expect(recoveredProtocol.request).not.toHaveBeenCalledWith('begin', expect.anything())
    expect(recoveredProtocol.request).not.toHaveBeenCalledWith('stage_configuration', expect.anything())
    expect(recoveredProtocol.request).not.toHaveBeenCalledWith('apply_configuration', expect.anything())
  })

  it('requires explicit E1002 confirmation before a clean flash', async () => {
    const esptool = { identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })), flash: vi.fn() }
    const brokenProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const session = createInstallerSession({ configuration, requestPort: async () => ({}), releaseLoader: async () => release, protocolFactory: () => brokenProtocol, esptool, partsLoader: async () => ({ eraseFlash: true, parts: [] }) })
    const phases = []
    session.subscribe((state) => phases.push(state.phase))
    await session.connect()
    expect(session.getState().phase).toBe('confirm-device')
    await session.confirmDevice()
    expect(esptool.flash).toHaveBeenCalledOnce()
    expect(session.getState().phase).toBe('reconnect')
    expect(phases).not.toContain('review')
  })

  it('installs E1001 with the shared release and saves its display profile after flashing', async () => {
    const bootloaderProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const unknownProtocol = appProtocol({ hardwareModel: 'unknown' })
    const unknownRequest = unknownProtocol.request.getMockImplementation()
    unknownProtocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'set_hardware_profile') return { status: 'reboot_required', hardwareProfileRevision: 1 }
      return unknownRequest(command, values, timeout)
    })
    const installedProtocol = appProtocol({ hardwareModel: 'e1001', hardwareProfileRevision: 1 })
    const protocolFactory = vi.fn()
      .mockReturnValueOnce(bootloaderProtocol)
      .mockReturnValueOnce(unknownProtocol)
      .mockReturnValueOnce(installedProtocol)
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(),
    }
    const releaseLoader = vi.fn(async () => release)
    const requestPort = vi.fn()
      .mockResolvedValueOnce({ id: 'initial-port' })
      .mockResolvedValueOnce({ id: 'profile-port' })
      .mockResolvedValueOnce({ id: 'installed-port' })
    const session = createInstallerSession({
      configuration: e1001Configuration(),
      requestPort,
      releaseLoader,
      partsLoader: async () => ({ eraseFlash: true, parts: [] }),
      protocolFactory,
      esptool,
      waitFor: vi.fn().mockResolvedValue(undefined),
    })

    await session.connect()
    await session.confirmDevice()
    await session.reconnect()
    await session.reconnect()

    expect(releaseLoader).toHaveBeenCalledWith(expect.objectContaining({ boardId: BOARD_IDS.E1001 }))
    expect(unknownProtocol.request).toHaveBeenCalledWith('set_hardware_profile', {
      hardwareModel: 'e1001', expectedRevision: 0,
    })
    expect(session.getState().phase).toBe('complete')
  })

  it.each([
    ['E1001', e1001Configuration(), 'hardware_profile_conflict'],
    ['E1002', { ...configuration, boardId: BOARD_IDS.E1002 }, 'hardware_profile_save_failed'],
  ])('stops post-flash %s setup when saving its display profile returns %s', async (
    _model,
    selectedConfiguration,
    profileStatus,
  ) => {
    const bootloaderProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const rejectedProtocol = appProtocol({ hardwareModel: 'unknown' })
    const rejectedRequest = rejectedProtocol.request.getMockImplementation()
    rejectedProtocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'set_hardware_profile') return { status: profileStatus }
      return rejectedRequest(command, values, timeout)
    })
    const session = createInstallerSession({
      configuration: selectedConfiguration,
      requestPort: vi.fn()
        .mockResolvedValueOnce({ id: 'initial-port' })
        .mockResolvedValueOnce({ id: 'profile-port' }),
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: true, parts: [] }),
      protocolFactory: vi.fn()
        .mockReturnValueOnce(bootloaderProtocol)
        .mockReturnValueOnce(rejectedProtocol),
      esptool: {
        identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
        flash: vi.fn(),
      },
    })

    await session.connect()
    await session.confirmDevice()
    await session.reconnect()

    expect(session.getState()).toMatchObject({
      phase: 'reconnect',
      safeToDisconnect: true,
      error: {
        code: INSTALLER_ERROR_CODES.INVALID_RESPONSE,
        message: 'Windpeek could not save the selected screen model.',
      },
    })
    const commands = rejectedProtocol.request.mock.calls.map(([command]) => command)
    expect(commands).not.toContain('begin')
    expect(commands).not.toContain('stage_configuration')
    expect(commands).not.toContain('apply_configuration')
  })

  it('ignores stale chooser responses after cancellation', async () => {
    let resolvePort
    const session = createInstallerSession({ configuration, requestPort: () => new Promise((resolve) => { resolvePort = resolve }) })
    const connecting = session.connect()
    await session.cancel()
    resolvePort({})
    await connecting
    expect(session.getState().phase).toBe('ready')
  })

  it('ignores a diagnostic result that arrives after cancellation', async () => {
    let finishReport
    const reporter = {
      report: vi.fn(() => new Promise((resolve) => { finishReport = resolve })),
    }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => { throw new Error('permission failed') },
      reporter,
    })

    await session.connect()
    await vi.waitFor(() => expect(session.getState().diagnosticStatus).toBe('sending'))
    await session.cancel()
    finishReport({ status: 'sent', reference: 'WS-STALE12345' })
    await Promise.resolve()

    expect(session.getState()).toMatchObject({
      phase: 'ready',
      diagnosticStatus: 'idle',
      diagnosticReference: null,
    })
  })

  it('does not expose an invalid diagnostic reference', async () => {
    const reporter = {
      report: vi.fn(async () => ({ status: 'sent', reference: 'not-a-reference' })),
    }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => { throw new Error('permission failed') },
      reporter,
    })

    await session.connect()
    await vi.waitFor(() => expect(session.getState().diagnosticStatus).toBe('failed'))

    expect(session.getState().diagnosticReference).toBeNull()
  })

  it('does not let cancelled configuration work overwrite the ready state', async () => {
    let finishStaging
    const protocol = appProtocol({ digest: 'old' })
    const originalRequest = protocol.request.getMockImplementation()
    protocol.request.mockImplementation(async (command, values, timeout) => {
      if (command === 'stage_configuration') {
        return new Promise((resolve) => { finishStaging = resolve })
      }
      return originalRequest(command, values, timeout)
    })
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })

    const running = session.connect()
    await vi.waitFor(() => expect(finishStaging).toBeTypeOf('function'))
    await session.cancel()
    finishStaging({ status: 'configuration_staged' })
    await running

    expect(session.getState()).toMatchObject({ phase: 'ready', progress: 0, action: null })
  })

  it('cannot disconnect the device while firmware bytes are being written', async () => {
    let finishFlash
    const oldProtocol = appProtocol({ firmwareVersion: '1.0.0' })
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(() => new Promise((resolve) => { finishFlash = resolve })),
    }
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: false, parts: [] }),
      protocolFactory: () => oldProtocol,
      esptool,
    })

    const running = session.connect()
    await vi.waitFor(() => expect(session.getState().phase).toBe('installing-firmware'))

    await session.cancel()
    expect(session.getState().phase).toBe('installing-firmware')

    finishFlash()
    await running
    expect(session.getState().phase).toBe('reconnect')
  })

  it('requires a new enclosure confirmation after cancellation', async () => {
    const esptool = { identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: { disconnect: vi.fn() } })), flash: vi.fn() }
    const brokenProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const session = createInstallerSession({ configuration, requestPort: async () => ({}), releaseLoader: async () => release, protocolFactory: () => brokenProtocol, esptool, partsLoader: async () => ({ eraseFlash: true, parts: [] }) })

    await session.connect()
    session.confirmDevice()
    await session.cancel()
    await session.connect()
    expect(session.getState().phase).toBe('confirm-device')
    expect(esptool.flash).not.toHaveBeenCalled()
  })

  it('waits for Chrome to expose and automatically reuse the granted port after restart', async () => {
    const oldProtocol = appProtocol({ firmwareVersion: '1.0.0' })
    const restartedProtocol = appProtocol()
    const protocolFactory = vi.fn()
      .mockReturnValueOnce(oldProtocol)
      .mockReturnValueOnce(restartedProtocol)
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(),
    }
    const initialPort = { id: 'initial-port' }
    const requestPort = vi.fn(async () => initialPort)
    const getPorts = vi.fn()
      .mockResolvedValueOnce([])
      .mockResolvedValueOnce([initialPort])
    const navigatorApi = { serial: { getPorts } }
    const waitFor = vi.fn().mockResolvedValue(undefined)
    const session = createInstallerSession({
      configuration,
      navigatorApi,
      requestPort,
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: false, parts: [] }),
      protocolFactory,
      esptool,
      waitFor,
    })

    await session.connect()
    expect(session.getState().action.action).toBe('update-firmware')
    expect(oldProtocol.close).toHaveBeenCalledBefore(esptool.identify)
    expect(getPorts).toHaveBeenCalledTimes(2)
    expect(waitFor).toHaveBeenCalledWith(500)
    expect(requestPort).toHaveBeenCalledOnce()
    expect(session.getState().phase).toBe('complete')
  })

  it('offers manual reconnection when Chrome never exposes the granted port again', async () => {
    const oldProtocol = appProtocol({ firmwareVersion: '1.0.0' })
    const initialPort = { id: 'initial-port' }
    const getPorts = vi.fn(async () => [])
    const waitFor = vi.fn().mockResolvedValue(undefined)
    const session = createInstallerSession({
      configuration,
      navigatorApi: { serial: { getPorts } },
      requestPort: async () => initialPort,
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: false, parts: [] }),
      protocolFactory: () => oldProtocol,
      esptool: {
        identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
        flash: vi.fn(),
      },
      waitFor,
    })

    await session.connect()

    expect(getPorts).toHaveBeenCalledTimes(20)
    expect(session.getState()).toMatchObject({ phase: 'reconnect', safeToDisconnect: true })
  })

  it('offers manual reconnection when granted-port discovery stalls', async () => {
    const oldProtocol = appProtocol({ firmwareVersion: '1.0.0' })
    const initialPort = { id: 'initial-port' }
    const reporter = { report: vi.fn().mockResolvedValue({ status: 'failed' }) }
    const session = createInstallerSession({
      configuration,
      navigatorApi: { serial: { getPorts: vi.fn(() => new Promise(() => {})) } },
      requestPort: async () => initialPort,
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: false, parts: [] }),
      protocolFactory: () => oldProtocol,
      esptool: {
        identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
        flash: vi.fn(),
      },
      portDiscoveryTimeoutMs: 0,
      reporter,
    })

    await session.connect()

    expect(session.getState()).toMatchObject({
      phase: 'reconnect',
      safeToDisconnect: true,
      error: { code: INSTALLER_ERROR_CODES.CONNECTION_LOST },
    })
    expect(reporter.report).toHaveBeenCalledOnce()
  })

  it('closes an in-progress automatic reconnect probe when cancelled', async () => {
    let finishHello
    const oldProtocol = appProtocol({ firmwareVersion: '1.0.0' })
    const restartedProtocol = {
      open: vi.fn(),
      close: vi.fn(),
      request: vi.fn((command) => command === 'hello'
        ? new Promise((resolve) => { finishHello = resolve })
        : Promise.resolve({})),
    }
    const protocolFactory = vi.fn()
      .mockReturnValueOnce(oldProtocol)
      .mockReturnValueOnce(restartedProtocol)
    const initialPort = { id: 'initial-port' }
    const session = createInstallerSession({
      configuration,
      navigatorApi: { serial: { getPorts: vi.fn(async () => [initialPort]) } },
      requestPort: async () => initialPort,
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: false, parts: [] }),
      protocolFactory,
      esptool: {
        identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
        flash: vi.fn(),
      },
    })

    const connecting = session.connect()
    await vi.waitFor(() => expect(finishHello).toBeTypeOf('function'))

    await session.cancel()

    expect(restartedProtocol.close).toHaveBeenCalled()
    expect(session.getState().phase).toBe('ready')
    finishHello({})
    await connecting
    expect(session.getState().phase).toBe('ready')
  })

  it('makes concurrent cancellations wait for the same USB teardown', async () => {
    let finishClose
    const protocol = appProtocol()
    protocol.close.mockImplementation(() => new Promise((resolve) => { finishClose = resolve }))
    const session = createInstallerSession({
      configuration,
      requestPort: async () => ({}),
      releaseLoader: async () => release,
      protocolFactory: () => protocol,
    })
    await session.connect()

    let secondCancellationFinished = false
    const firstCancellation = session.cancel()
    const secondCancellation = session.cancel().then(() => { secondCancellationFinished = true })
    await vi.waitFor(() => expect(finishClose).toBeTypeOf('function'))

    expect(secondCancellationFinished).toBe(false)
    finishClose()
    await Promise.all([firstCancellation, secondCancellation])

    expect(protocol.close).toHaveBeenCalledOnce()
    expect(session.getState().phase).toBe('ready')
  })

  it('waits for a freshly written device to boot instead of starting the firmware install again', async () => {
    const bootloaderProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const stillBootingProtocol = { open: vi.fn().mockRejectedValue(new Error('still booting')), close: vi.fn() }
    const restartedProtocol = appProtocol()
    const protocolFactory = vi.fn()
      .mockReturnValueOnce(bootloaderProtocol)
      .mockReturnValueOnce(stillBootingProtocol)
      .mockReturnValueOnce(restartedProtocol)
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(),
    }
    const waitFor = vi.fn().mockResolvedValue(undefined)
    const requestPort = vi.fn()
      .mockResolvedValueOnce({ id: 'initial-port' })
      .mockResolvedValueOnce({ id: 'restarted-port' })
    const session = createInstallerSession({
      configuration,
      requestPort,
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: true, parts: [] }),
      protocolFactory,
      esptool,
      waitFor,
    })

    await session.connect()
    await session.confirmDevice()
    expect(session.getState().phase).toBe('reconnect')

    await session.reconnect()

    expect(waitFor).toHaveBeenCalled()
    expect(esptool.identify).toHaveBeenCalledOnce()
    expect(esptool.flash).toHaveBeenCalledOnce()
    expect(session.getState().phase).toBe('complete')
  })

  it('keeps reconnect safe when freshly written firmware takes too long to boot', async () => {
    const unavailableProtocol = () => ({ open: vi.fn().mockRejectedValue(new Error('still booting')), close: vi.fn() })
    const protocolFactory = vi.fn(() => unavailableProtocol())
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(),
    }
    const requestPort = vi.fn()
      .mockResolvedValueOnce({ id: 'initial-port' })
      .mockResolvedValueOnce({ id: 'restarted-port' })
    const session = createInstallerSession({
      configuration,
      requestPort,
      releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: true, parts: [] }),
      protocolFactory,
      esptool,
      waitFor: vi.fn().mockResolvedValue(undefined),
    })

    await session.connect()
    await session.confirmDevice()
    await session.reconnect()

    expect(esptool.identify).toHaveBeenCalledOnce()
    expect(esptool.flash).toHaveBeenCalledOnce()
    expect(session.getState()).toMatchObject({ phase: 'reconnect', safeToDisconnect: true })
    expect(session.getState().error?.message).toMatch(/still restarting/i)
  })

  it('asks for reconnect when state probing or configuration loses USB', async () => {
    const protocol = appProtocol({ digest: 'old' })
    protocol.request.mockImplementationOnce(async () => ({
      status: 'ok', boardId: release.manifest.boardId, chipFamily: 'ESP32-S3',
        firmwareVersion: '2.0.0', protocolVersion: 1, configurationVersion: CONFIGURATION_VERSION,
      capabilities: ['state', 'wifi', 'configuration', 'render-verification', 'clock-sync'],
    }))
      .mockRejectedValueOnce(new Error('disconnected'))
    const session = createInstallerSession({ configuration, requestPort: async () => ({}), releaseLoader: async () => release, protocolFactory: () => protocol })
    await session.connect()
    expect(session.getState().phase).toBe('reconnect')
  })

  it('recovers an interrupted flash through an explicit reconnect and confirmation', async () => {
    const transport = { disconnect: vi.fn() }
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport })),
      flash: vi.fn().mockRejectedValueOnce(new InstallerError(
        INSTALLER_ERROR_CODES.FLASH_FAILED,
        'Firmware installation was interrupted.',
        { safeToDisconnect: true },
      )),
    }
    const brokenProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const requestPort = vi.fn()
      .mockResolvedValueOnce({ id: 'initial-port' })
      .mockResolvedValueOnce({ id: 'reconnected-port' })
    const session = createInstallerSession({
      configuration, requestPort, releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: true, parts: [] }),
      protocolFactory: () => brokenProtocol, esptool,
    })

    await session.connect()
    await session.confirmDevice()
    expect(session.getState().phase).toBe('reconnect')

    await session.reconnect()
    expect(session.getState().phase).toBe('confirm-device')
    expect(requestPort).toHaveBeenCalledTimes(2)
  })

  it('keeps reconnect recoverable when the device chooser fails', async () => {
    const brokenProtocol = { open: vi.fn().mockRejectedValue(new Error('no app')), close: vi.fn() }
    const esptool = {
      identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', loader: {}, transport: {} })),
      flash: vi.fn(),
    }
    const requestPort = vi.fn()
      .mockResolvedValueOnce({ id: 'initial-port' })
      .mockRejectedValueOnce(new Error('permission failed'))
    const session = createInstallerSession({
      configuration, requestPort, releaseLoader: async () => release,
      partsLoader: async () => ({ eraseFlash: true, parts: [] }),
      protocolFactory: () => brokenProtocol, esptool,
    })

    await session.connect()
    await session.confirmDevice()
    await session.reconnect()

    expect(session.getState()).toMatchObject({ phase: 'reconnect', safeToDisconnect: true })
    expect(session.getState().error).toBeInstanceOf(InstallerError)
  })

  it('never trusts an incomplete hello as a verified E1002', async () => {
    const incomplete = {
      open: vi.fn(), close: vi.fn(),
      request: vi.fn(async () => ({ boardId: release.manifest.boardId, firmwareVersion: '2.0.0' })),
    }
    const esptool = { identify: vi.fn(async () => ({ chipFamily: 'ESP32-S3', transport: {} })) }
    const session = createInstallerSession({
      configuration, requestPort: async () => ({}), releaseLoader: async () => release,
      protocolFactory: () => incomplete, esptool,
    })
    await session.connect()
    expect(incomplete.close).toHaveBeenCalled()
    expect(session.getState().phase).toBe('confirm-device')
  })
})
