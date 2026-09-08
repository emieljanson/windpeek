import { resolveInstallAction, INSTALL_ACTIONS } from './actionResolver'
import {
  CHIP_FAMILY,
  installerReleaseBoardId,
  loadFirmwareParts,
  loadFirmwareRelease,
} from './firmwareManifest'
import {
  BOARD_ID,
  BOARD_IDS,
  CONFIGURATION_VERSION,
  installedConfigurationDigest,
} from '../config/configuration'
import { asInstallerError, InstallerError, INSTALLER_ERROR_CODES } from './installerErrors'
import { createEsptoolAdapter } from './esptoolAdapter'
import { createInstallerDiagnostics } from './installerDiagnostics'
import { installerSentryReporter, isInstallerDiagnosticReference } from './sentryReporter'
import { createSerialProtocol, findGrantedInstallerPort, requestInstallerPort } from './serialPortAdapter'

const INITIAL_STATE = Object.freeze({
  phase: 'ready', progress: 0, safeToDisconnect: true, error: null, action: null,
  diagnosticStatus: 'idle', diagnosticReference: null,
})
const REQUIRED_CAPABILITIES = ['state', 'wifi', 'configuration', 'render-verification', 'clock-sync']
// A failed candidate may take 45 seconds, followed by up to 45 seconds to
// restore the previously saved network. Keep enough serial margin for both.
const WIFI_TEST_REQUEST_TIMEOUT_MS = 105000
// Rendering the first full e-paper preview performs network parsing and image
// composition on the device. The UART may be unable to answer a status poll
// during that work even though the apply task is still healthy, so keep this
// individual request alive for the same generous production window.
const APPLY_STATUS_REQUEST_TIMEOUT_MS = 120000
const PORT_DISCOVERY_TIMEOUT_MS = 2000
const POST_FLASH_PORT_DISCOVERY_ATTEMPTS = 20
const POST_FLASH_PORT_DISCOVERY_RETRY_MS = 500
const POST_FLASH_APP_BOOT_ATTEMPTS = 20
const POST_FLASH_APP_BOOT_RETRY_MS = 500
const SAVED_WIFI_CONNECT_ATTEMPTS = 12
const SAVED_WIFI_CONNECT_RETRY_MS = 500
const MIN_UPGRADEABLE_CONFIGURATION_VERSION = 2
const rememberedInstallerPorts = new WeakMap()
let diagnosticSessionSequence = 0

function validHello(hello) {
  return hello?.status === 'ok' && typeof hello.boardId === 'string' &&
    typeof hello.firmwareVersion === 'string' && hello.protocolVersion === 1 &&
    Number.isInteger(hello.configurationVersion) &&
    hello.configurationVersion >= MIN_UPGRADEABLE_CONFIGURATION_VERSION &&
    hello.configurationVersion <= CONFIGURATION_VERSION && Array.isArray(hello.capabilities) &&
    REQUIRED_CAPABILITIES.every((capability) => hello.capabilities.includes(capability))
}

export function createInstallerSession({
  configuration,
  navigatorApi = globalThis.navigator,
  releaseLoader = loadFirmwareRelease,
  partsLoader = loadFirmwareParts,
  diagnostics = createInstallerDiagnostics(),
  reporter = installerSentryReporter,
  protocolFactory = createSerialProtocol,
  esptool = createEsptoolAdapter({ diagnostics }),
  requestPort = () => requestInstallerPort(navigatorApi),
  waitFor = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)),
  portDiscoveryTimeoutMs = PORT_DISCOVERY_TIMEOUT_MS,
  now = () => Date.now(),
} = {}) {
  let state = { ...INITIAL_STATE }
  let port = null
  let protocol = null
  let release = null
  let device = null
  let action = null
  let attempt = 0
  let confirmed = false
  let operationController = null
  let failureOccurrence = 0
  let latestDiagnosticOccurrence = null
  let awaitingWrittenFirmware = false
  let cancellationPromise = null
  const diagnosticSession = ++diagnosticSessionSequence
  const pendingFailures = []
  const listeners = new Set()
  const probingProtocols = new Set()
  const expectedHardwareModel = configuration?.boardId || BOARD_ID
  const releaseBoardId = installerReleaseBoardId(expectedHardwareModel)
  const installationConfiguration = expectedHardwareModel === BOARD_IDS.E1001 &&
      configuration?.version === CONFIGURATION_VERSION
    ? (() => {
        const compatible = { ...configuration, boardId: BOARD_IDS.E1002 }
        compatible.digest = installedConfigurationDigest(compatible)
        return compatible
      })()
    : configuration

  try {
    diagnostics.registerSensitiveValues?.([
      configuration?.spot?.id,
      configuration?.spot?.name,
      configuration?.spot?.timezone,
    ])
    diagnostics.setContext?.({
      boardId: releaseBoardId,
      selectedBoardId: expectedHardwareModel,
      chipFamily: CHIP_FAMILY,
    })
  } catch {}

  function update(patch) {
    const previousPhase = state.phase
    state = { ...state, ...patch }
    try { diagnostics.setContext?.({ phase: state.phase, action: state.action?.action, attempt }) } catch {}
    if (state.phase !== previousPhase) {
      try { diagnostics.record?.({ category: 'state', operation: state.phase, status: 'entered' }) } catch {}
    }
    for (const listener of listeners) listener(state)
  }

  function completeAttempt(patch = {}) {
    latestDiagnosticOccurrence = null
    update({
      phase: 'complete', progress: 1, safeToDisconnect: true,
      diagnosticStatus: 'idle', diagnosticReference: null, ...patch,
    })
    try { diagnostics.destroy?.() } catch {}
  }

  function resetDiagnosticDelivery() {
    latestDiagnosticOccurrence = null
    update({ diagnosticStatus: 'idle', diagnosticReference: null })
  }

  function setReleaseDiagnosticContext() {
    try {
      diagnostics.setContext?.({
        release: release?.manifest?.version,
        route: action?.action,
        boardId: device?.boardId ?? release?.manifest?.boardId,
        chipFamily: device?.chipFamily ?? release?.manifest?.chipFamily,
        layoutVersion: device?.firmwareLayoutVersion ?? release?.manifest?.firmwareLayoutVersion,
        selectedBoardId: expectedHardwareModel,
        detectedBoardId: device?.boardId ?? 'unknown',
        detectedFirmwareVersion: device?.firmwareVersion ?? 'unknown',
        releaseBoardId: release?.manifest?.boardId ?? 'unknown',
        releaseVersion: release?.manifest?.version ?? 'unknown',
        connectionKind: device?.kind ?? 'unknown',
        decisionReason: action?.reason ?? 'unknown',
      })
    } catch {}
  }

  function isReportable(error) {
    return ![
      INSTALLER_ERROR_CODES.UNSUPPORTED,
    ].includes(error?.code)
  }

  function sendFailure(failure) {
    let snapshot
    try {
      snapshot = diagnostics.snapshot?.()
    } catch {
      if (failure.attempt === attempt) update({ diagnosticStatus: 'failed', diagnosticReference: null })
      return
    }
    if (!snapshot) {
      pendingFailures.push(failure)
      return
    }
    latestDiagnosticOccurrence = failure.occurrence
    update({ diagnosticStatus: 'sending', diagnosticReference: null })
    let report
    try {
      report = reporter.report({ ...failure, snapshot })
    } catch (error) {
      report = Promise.reject(error)
    }
    Promise.resolve(report)
      .then((result) => {
        if (failure.attempt !== attempt || latestDiagnosticOccurrence !== failure.occurrence) return
        const sent = result?.status === 'sent' && isInstallerDiagnosticReference(result.reference)
        update({
          diagnosticStatus: sent ? 'sent' : 'failed',
          diagnosticReference: sent ? result.reference : null,
        })
      })
      .catch(() => {
        if (failure.attempt === attempt && latestDiagnosticOccurrence === failure.occurrence) {
          update({ diagnosticStatus: 'failed', diagnosticReference: null })
        }
      })
  }

  function flushPendingFailures() {
    try {
      if (diagnostics.credentialsLocked) return
    } catch {
      pendingFailures.length = 0
      update({ diagnosticStatus: 'failed', diagnosticReference: null })
      return
    }
    for (const failure of pendingFailures.splice(0)) sendFailure(failure)
  }

  function reportFailure(error, phase = state.phase) {
    if (!isReportable(error)) return
    const failure = {
      attempt,
      occurrence: `${diagnosticSession}:${attempt}:${++failureOccurrence}`,
      phase,
      error,
    }
    try {
      diagnostics.setContext?.({ phase, errorCode: error?.code, action: action?.action, attempt })
      diagnostics.record?.({ category: 'installer', operation: phase, status: 'failed', message: error?.message })
    } catch {}
    sendFailure(failure)
  }

  function recordVerificationFailure(status) {
    try {
      diagnostics.record?.({
        category: 'verification',
        operation: 'device-state',
        status: status?.apply ?? 'incomplete',
      })
    } catch {}
  }

  async function releaseConnections({ clearDevice = false } = {}) {
    const activeProtocol = protocol
    const bootloaderTransport = device?.bootloader?.transport
    const activeProbes = [...probingProtocols]
    protocol = null
    probingProtocols.clear()
    if (clearDevice) device = null
    await Promise.allSettled([
      activeProtocol?.close(),
      bootloaderTransport?.disconnect?.(),
      ...activeProbes.map((candidate) => candidate.close()),
    ].filter(Boolean))
  }

  function isCurrent(expectedAttempt) {
    return expectedAttempt === attempt
  }

  function rememberVerifiedPort(selectedPort = port) {
    if (device?.verifiedBoard && selectedPort && navigatorApi?.serial) {
      rememberedInstallerPorts.set(navigatorApi.serial, selectedPort)
    }
  }

  async function findGrantedPort(options) {
    let timeoutId
    const timeout = new Promise((_, reject) => {
      timeoutId = setTimeout(() => reject(new InstallerError(
        INSTALLER_ERROR_CODES.CONNECTION_LOST,
        'Windpeek could not check previously granted USB devices.',
      )), portDiscoveryTimeoutMs)
    })
    try {
      return await Promise.race([findGrantedInstallerPort(options), timeout])
    } finally {
      clearTimeout(timeoutId)
    }
  }

  async function findRememberedPort(rememberedPort) {
    if (!navigatorApi?.serial?.getPorts) return null
    try {
      return await findGrantedPort({
        navigatorApi,
        signal: operationController?.signal,
        classify: (candidate) => candidate === rememberedPort,
      })
    } catch {
      return null
    }
  }

  async function retryChooserAfterStaleRememberedPort(expectedAttempt) {
    rememberedInstallerPorts.delete(navigatorApi.serial)
    await releaseConnections({ clearDevice: true })
    if (!isCurrent(expectedAttempt)) return state
    return connect()
  }

  async function inspectApp(candidate) {
    try {
      await candidate.open()
    } catch (error) {
      try { await candidate.close() } catch {}
      if (error?.name === 'NetworkError') {
        throw new InstallerError(
          INSTALLER_ERROR_CODES.DEVICE_NOT_ALLOWED,
          'This USB device is already in use. Close other Windpeek tabs or serial tools, then try again.',
          { cause: error },
        )
      }
      return null
    }
    let hello
    try {
      hello = await candidate.request('hello')
      if (!validHello(hello)) throw new Error('Incomplete Windpeek identity')
    } catch {
      try { await candidate.close() } catch {}
      return null
    }
    try {
      let current = await candidate.request('get_state')
      // Opening Web Serial performs a normal device reset. A saved network
      // commonly needs another second or two to obtain an IP address, so do
      // not mistake that short boot window for missing Wi-Fi setup.
      for (let wifiAttempt = 1;
        current.wifiConfigured === true && current.wifi !== 'connected' &&
        wifiAttempt < SAVED_WIFI_CONNECT_ATTEMPTS;
        wifiAttempt += 1) {
        await waitFor(SAVED_WIFI_CONNECT_RETRY_MS)
        current = await candidate.request('get_state')
      }
      const usesHardwareProfile = hello.capabilities.includes('hardware-profile')
      const reportedHardwareModel = usesHardwareProfile
        ? ({ e1001: BOARD_IDS.E1001, e1002: BOARD_IDS.E1002 }[hello.hardwareModel])
        : undefined
      return {
        protocol: candidate,
        device: {
          kind: 'windpeek',
          verifiedBoard: (usesHardwareProfile ? reportedHardwareModel : hello.boardId) === expectedHardwareModel,
          hardwareModelMismatch: usesHardwareProfile && reportedHardwareModel !== undefined &&
            reportedHardwareModel !== expectedHardwareModel,
          boardId: hello.boardId,
          hardwareModel: hello.hardwareModel,
          hardwareProfileRevision: hello.hardwareProfileRevision,
          capabilities: hello.capabilities,
          chipFamily: hello.chipFamily ?? CHIP_FAMILY,
          firmwareVersion: hello.firmwareVersion,
          configurationVersion: hello.configurationVersion,
          // Firmware released before this field was introduced uses the same
          // original partition layout, so it is safely version 1.
          firmwareLayoutVersion: hello.firmwareLayoutVersion ?? 1,
          configurationDigest: current.configurationDigest ?? null,
          wifiHealthy: current.wifiHealthy ?? current.wifi === 'connected',
          wifiConfigured: current.wifiConfigured === true,
          renderValid: current.render === 'valid',
          applyState: current.apply ?? 'idle',
          damaged: false,
        },
      }
    } catch (error) {
      try { await candidate.close() } catch {}
      throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'Windpeek disconnected while its setup was checked.', { cause: error })
    }
  }

  async function probeApp(selectedPort) {
    const candidate = protocolFactory(selectedPort, { diagnostics })
    probingProtocols.add(candidate)
    try {
      return await inspectApp(candidate)
    } finally {
      probingProtocols.delete(candidate)
    }
  }

  async function connect() {
    const currentAttempt = ++attempt
    confirmed = false
    awaitingWrittenFirmware = false
    operationController?.abort()
    operationController = new AbortController()
    const rememberedPort = navigatorApi?.serial
      ? rememberedInstallerPorts.get(navigatorApi.serial)
      : null
    let selectedPort = rememberedPort ? await findRememberedPort(rememberedPort) : null
    const usingRememberedPort = Boolean(rememberedPort && selectedPort === rememberedPort)
    if (!isCurrent(currentAttempt)) return state
    if (!selectedPort) {
      update({ phase: 'choosing-device', error: null, diagnosticStatus: 'idle', diagnosticReference: null })
      try {
        selectedPort = await requestPort()
      } catch (error) {
        if (currentAttempt !== attempt) return state
        const installerError = asInstallerError(error, INSTALLER_ERROR_CODES.DEVICE_NOT_ALLOWED, 'Windpeek could not access the selected USB device.')
        update({ phase: 'error', error: installerError, safeToDisconnect: true })
        reportFailure(installerError, 'choosing-device')
        return state
      }
    }
    if (currentAttempt !== attempt) return state
    if (!selectedPort) {
      update({ ...INITIAL_STATE, phase: 'ready' })
      return state
    }
    port = selectedPort
    update({ phase: 'checking-device' })
    try {
      await releaseConnections({ clearDevice: true })
      const [releaseResult, probeResult] = await Promise.allSettled([
        releaseLoader({ boardId: expectedHardwareModel, signal: operationController.signal }),
        probeApp(port),
      ])
      const appProbe = probeResult.status === 'fulfilled' ? probeResult.value : null
      if (releaseResult.status === 'rejected') {
        try { await appProbe?.protocol.close() } catch {}
        throw releaseResult.reason
      }
      if (probeResult.status === 'rejected') {
        if (usingRememberedPort) return retryChooserAfterStaleRememberedPort(currentAttempt)
        throw probeResult.reason
      }
      const loadedRelease = releaseResult.value
      if (currentAttempt !== attempt) {
        try { await appProbe?.protocol.close() } catch {}
        return state
      }
      release = loadedRelease
      protocol = appProbe?.protocol ?? null
      device = appProbe?.device ?? null
      if (!device) {
        let identity
        try {
          identity = await esptool.identify(port)
        } catch (error) {
          if (usingRememberedPort) return retryChooserAfterStaleRememberedPort(currentAttempt)
          throw error
        }
        if (currentAttempt !== attempt) {
          try { await identity.transport?.disconnect() } catch {}
          return state
        }
        device = { kind: 'rom', chipFamily: identity.chipFamily, verifiedBoard: false, firmwareVersion: null, bootloader: identity }
      }
      action = resolveInstallAction({
        device,
        release: release.manifest,
        configurationDigest: installationConfiguration.digest,
        requiredConfigurationVersion: CONFIGURATION_VERSION,
      })
      setReleaseDiagnosticContext()
      if (action.action === INSTALL_ACTIONS.BLOCKED) {
        throw new InstallerError(INSTALLER_ERROR_CODES.INCOMPATIBLE_DEVICE, 'This is not the selected reTerminal model.', { recoverable: false })
      }
      rememberVerifiedPort(selectedPort)
      if (action.action === INSTALL_ACTIONS.CONFIRM_E1002) {
        update({ phase: 'confirm-device', action })
      } else {
        return await executeAction()
      }
    } catch (error) {
      if (currentAttempt !== attempt) return state
      await releaseConnections({ clearDevice: true })
      const installerError = asInstallerError(error, INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'Windpeek could not check this device.')
      update({ phase: installerError.code === INSTALLER_ERROR_CODES.CONNECTION_LOST ? 'reconnect' : 'error', error: installerError, safeToDisconnect: installerError.safeToDisconnect })
      reportFailure(installerError, 'checking-device')
    }
    return state
  }

  async function confirmDevice() {
    if (state.phase !== 'confirm-device') return state
    confirmed = true
    action = { action: INSTALL_ACTIONS.INSTALL, reason: 'confirmed-e1002' }
    return executeAction()
  }

  async function scanNetworks() {
    if (!protocol) return []
    const scanAttempt = attempt
    resetDiagnosticDelivery()
    try {
      const response = await protocol.request('scan_networks', {}, 45000)
      return Array.isArray(response.networks) ? response.networks : []
    } catch (error) {
      const installerError = asInstallerError(error, INSTALLER_ERROR_CODES.WIFI_FAILED, 'Windpeek could not scan for Wi-Fi networks.')
      // A network scan can finish after the user has already submitted the
      // chosen network. Its late failure must not pull a successfully applying
      // or completed installer back to the Wi-Fi form.
      if (scanAttempt === attempt && state.phase === 'wifi') {
        update({ phase: 'wifi', error: installerError, safeToDisconnect: true })
        reportFailure(installerError, 'wifi')
      }
      throw installerError
    }
  }

  async function configure(credentials, expectedAttempt = attempt) {
    if (!protocol) throw new InstallerError(INSTALLER_ERROR_CODES.CONNECTION_LOST, 'Select your reTerminal again to finish setup.')
    update({ phase: 'configuring', progress: 0.82, safeToDisconnect: true, action })
    const unixTime = Math.floor(now() / 1000)
    if (!Number.isSafeInteger(unixTime)) {
      throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'Windpeek could not read this computer’s time.')
    }
    const begun = await protocol.request('begin', { unixTime })
    if (begun.status === 'clock_rejected') {
      throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'Windpeek could not set its clock from this computer.')
    }
    if (!isCurrent(expectedAttempt)) return false
    const staged = await protocol.request('stage_configuration', {
      configuration: installationConfiguration,
    })
    if (!isCurrent(expectedAttempt)) return false
    if (staged.status !== 'configuration_staged') throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'Windpeek rejected this configuration.')
    if (credentials) {
      const wifi = await protocol.request('test_wifi', credentials, WIFI_TEST_REQUEST_TIMEOUT_MS)
      if (!isCurrent(expectedAttempt)) return false
      if (wifi.status !== 'wifi_ready') throw new InstallerError(INSTALLER_ERROR_CODES.WIFI_FAILED, 'Windpeek could not connect to that Wi-Fi network.')
    }
    update({ phase: 'verifying', progress: 0.92 })
    const applied = await protocol.request('apply_configuration', undefined, APPLY_STATUS_REQUEST_TIMEOUT_MS)
    if (!isCurrent(expectedAttempt)) return false
    if (!['applying', 'complete'].includes(applied.status)) {
      recordVerificationFailure({ apply: applied.status })
      throw new InstallerError(INSTALLER_ERROR_CODES.VERIFICATION_FAILED, 'Windpeek could not apply the new setup.')
    }
    for (let poll = 0; poll < 180; poll += 1) {
      if (poll > 0 || applied.status === 'applying') await waitFor(1000)
      if (!isCurrent(expectedAttempt)) return false
      const status = await protocol.request('get_state', undefined, APPLY_STATUS_REQUEST_TIMEOUT_MS)
      if (!isCurrent(expectedAttempt)) return false
      if (['render_failed', 'commit_failed'].includes(status.apply)) {
        recordVerificationFailure(status)
        throw new InstallerError(INSTALLER_ERROR_CODES.VERIFICATION_FAILED, 'Windpeek could not apply the new setup.')
      }
      const applyComplete = status.apply === 'complete' ||
        (applied.status === 'complete' && [undefined, 'idle'].includes(status.apply))
      if (applyComplete && status.configurationDigest === installationConfiguration.digest &&
          status.wifi === 'connected' && status.render === 'valid') return true
    }
    throw new InstallerError(INSTALLER_ERROR_CODES.VERIFICATION_FAILED, 'Windpeek could not verify the new setup.')
  }

  async function verifyCurrentConfiguration(expectedAttempt, initialDevice) {
    update({ phase: 'verifying', progress: 0.92, safeToDisconnect: true })
    if (initialDevice.configurationDigest === installationConfiguration.digest &&
        initialDevice.wifiHealthy && initialDevice.renderValid) return true
    for (let poll = 0; poll < 60; poll += 1) {
      await waitFor(1000)
      if (!isCurrent(expectedAttempt)) return false
      const status = await protocol.request('get_state', undefined, APPLY_STATUS_REQUEST_TIMEOUT_MS)
      if (!isCurrent(expectedAttempt)) return false
      if (['render_failed', 'commit_failed'].includes(status.apply)) {
        recordVerificationFailure(status)
        throw new InstallerError(INSTALLER_ERROR_CODES.VERIFICATION_FAILED, 'Windpeek could not apply the new setup.')
      }
      if (status.configurationDigest === installationConfiguration.digest &&
          status.wifi === 'connected' && status.render === 'valid') return true
    }
    throw new InstallerError(INSTALLER_ERROR_CODES.VERIFICATION_FAILED, 'Windpeek could not verify the current setup.')
  }

  async function executeAction() {
    const currentAttempt = attempt
    try {
      if (action.action === INSTALL_ACTIONS.UP_TO_DATE) {
        completeAttempt({ action })
        return state
      }
      if ([INSTALL_ACTIONS.INSTALL, INSTALL_ACTIONS.REINSTALL, INSTALL_ACTIONS.UPDATE_FIRMWARE].includes(action.action)) {
        if (!device.verifiedBoard && !confirmed) throw new InstallerError(INSTALLER_ERROR_CODES.UNCONFIRMED_DEVICE, 'Confirm this is the selected reTerminal model before installing.')
        const mode = action.action === INSTALL_ACTIONS.UPDATE_FIRMWARE ? 'preservingUpdate' : 'cleanInstall'
        update({ phase: 'downloading', progress: 0.05, action })
        const bundle = await partsLoader({
          ...release,
          boardId: expectedHardwareModel,
          mode,
          signal: operationController?.signal,
        })
        if (currentAttempt !== attempt) return state
        update({ phase: 'installing-firmware', progress: 0.15, safeToDisconnect: false })
        if (!device.bootloader) {
          await protocol?.close()
          protocol = null
        }
        const identity = device.bootloader ?? await esptool.identify(port)
        const totalBytes = bundle.parts.reduce((sum, part) => sum + part.size, 0)
        await esptool.flash({
          ...identity,
          bundle,
          onProgress: ({ fileIndex, written }) => {
            const completedBytes = bundle.parts.slice(0, fileIndex).reduce((sum, part) => sum + part.size, 0)
            if (isCurrent(currentAttempt)) {
              update({ progress: 0.15 + ((completedBytes + written) / totalBytes) * 0.62 })
            }
          },
        })
        awaitingWrittenFirmware = true
        protocol = null
        await reconnectGrantedPort(currentAttempt)
        return state
      }
      if (!device.wifiHealthy) {
        update({ phase: 'wifi', progress: 0.8, safeToDisconnect: true, action })
        return state
      }
      if (!await configure(undefined, currentAttempt)) return state
      completeAttempt()
    } catch (error) {
      if (currentAttempt !== attempt) return state
      const installerError = asInstallerError(error, INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'Windpeek setup could not continue.')
      update({
        phase: installerError.code === INSTALLER_ERROR_CODES.FLASH_FAILED ? 'reconnect' : 'error',
        error: installerError,
        safeToDisconnect: installerError.safeToDisconnect,
      })
      reportFailure(installerError, state.phase)
    }
    return state
  }

  async function attachReconnectedPort(reconnectedPort, expectedAttempt = attempt) {
    port = reconnectedPort
    let appProbe = await probeApp(port)
    if (!appProbe && awaitingWrittenFirmware) {
      for (let bootAttempt = 1; bootAttempt < POST_FLASH_APP_BOOT_ATTEMPTS && !appProbe; bootAttempt += 1) {
        await waitFor(POST_FLASH_APP_BOOT_RETRY_MS)
        if (expectedAttempt !== attempt) return state
        appProbe = await probeApp(port)
      }
    }
    if (expectedAttempt !== attempt) {
      try { await appProbe?.protocol.close() } catch {}
      return state
    }
    if (!appProbe) {
      if (awaitingWrittenFirmware) {
        throw new InstallerError(
          INSTALLER_ERROR_CODES.CONNECTION_LOST,
          'Windpeek is still restarting. Wait a moment, then select it again.',
        )
      }
      const identity = await esptool.identify(port)
      if (expectedAttempt !== attempt) {
        try { await identity.transport?.disconnect() } catch {}
        return state
      }
      device = {
        kind: 'rom', chipFamily: identity.chipFamily, verifiedBoard: false,
        firmwareVersion: null, bootloader: identity,
      }
      release ??= await releaseLoader({
        boardId: expectedHardwareModel,
        signal: operationController?.signal,
      })
      if (expectedAttempt !== attempt) {
        try { await identity.transport?.disconnect() } catch {}
        return state
      }
      action = resolveInstallAction({
        device,
        release: release.manifest,
        configurationDigest: installationConfiguration.digest,
        requiredConfigurationVersion: CONFIGURATION_VERSION,
      })
      setReleaseDiagnosticContext()
      if (action.action === INSTALL_ACTIONS.BLOCKED) {
        throw new InstallerError(INSTALLER_ERROR_CODES.INCOMPATIBLE_DEVICE, 'This is not the selected reTerminal model.', { recoverable: false })
      }
      confirmed = false
      update({ phase: 'confirm-device', progress: 0, safeToDisconnect: true, action })
      return state
    }
    protocol = appProbe.protocol
    device = appProbe.device
    if (awaitingWrittenFirmware &&
        [BOARD_IDS.E1001, BOARD_IDS.E1002].includes(expectedHardwareModel) &&
        device.capabilities?.includes('hardware-profile') &&
        device.hardwareModel === 'unknown') {
      const selected = await protocol.request('set_hardware_profile', {
        hardwareModel: expectedHardwareModel === BOARD_IDS.E1001 ? 'e1001' : 'e1002',
        expectedRevision: device.hardwareProfileRevision ?? 0,
      })
      if (!['reboot_required', 'hardware_profile_saved'].includes(selected.status)) {
        throw new InstallerError(
          INSTALLER_ERROR_CODES.INVALID_RESPONSE,
          'Windpeek could not save the selected screen model.',
        )
      }
      if (selected.status === 'reboot_required') {
        await protocol.close()
        protocol = null
        await waitFor(POST_FLASH_APP_BOOT_RETRY_MS)
        return reconnectGrantedPort(expectedAttempt)
      }
    }
    rememberVerifiedPort(reconnectedPort)
    if (device.configurationDigest === installationConfiguration.digest && device.wifiHealthy) {
      if (!await verifyCurrentConfiguration(expectedAttempt, device)) return state
      completeAttempt()
    } else if (!device.wifiHealthy) update({ phase: 'wifi', progress: 0.8, safeToDisconnect: true })
    else {
      if (!await configure(undefined, expectedAttempt)) return state
      completeAttempt()
    }
    return state
  }

  async function reconnectGrantedPort(expectedAttempt = attempt) {
    const previouslySelectedPort = port
    update({ phase: 'reconnecting', progress: 0.78, error: null, safeToDisconnect: true })
    if (!navigatorApi?.serial?.getPorts) {
      update({ phase: 'reconnect', safeToDisconnect: true })
      return state
    }
    try {
      let grantedPort = null
      for (let searchAttempt = 0; searchAttempt < POST_FLASH_PORT_DISCOVERY_ATTEMPTS && !grantedPort; searchAttempt += 1) {
        grantedPort = await findGrantedPort({
          navigatorApi,
          signal: operationController?.signal,
          classify: (candidate) => candidate === previouslySelectedPort,
        })
        if (!isCurrent(expectedAttempt)) return state
        if (!grantedPort && searchAttempt < POST_FLASH_PORT_DISCOVERY_ATTEMPTS - 1) {
          await waitFor(POST_FLASH_PORT_DISCOVERY_RETRY_MS)
          if (!isCurrent(expectedAttempt)) return state
        }
      }
      if (!grantedPort) {
        update({ phase: 'reconnect', safeToDisconnect: true })
        return state
      }
      return await attachReconnectedPort(grantedPort, expectedAttempt)
    } catch (error) {
      if (!isCurrent(expectedAttempt)) return state
      const installerError = asInstallerError(error, INSTALLER_ERROR_CODES.CONNECTION_LOST, 'Windpeek did not reconnect automatically.')
      update({ phase: 'reconnect', error: installerError, safeToDisconnect: true })
      reportFailure(installerError, 'reconnect')
      return state
    }
  }

  async function reconnect() {
    const currentAttempt = ++attempt
    resetDiagnosticDelivery()
    operationController?.abort()
    operationController = new AbortController()
    update({ phase: 'reconnecting', error: null, safeToDisconnect: true })
    // A timed-out read cancels the stream but may leave its locks and port
    // open. Release that session before asking Chrome to grant the port again.
    try { await protocol?.close() } catch {}
    protocol = null
    try {
      const selectedPort = await requestPort()
      if (currentAttempt !== attempt) return state
      if (!selectedPort) {
        update({ phase: 'reconnect', safeToDisconnect: true })
        return state
      }
      return await attachReconnectedPort(selectedPort, currentAttempt)
    } catch (error) {
      if (currentAttempt !== attempt) return state
      await releaseConnections({ clearDevice: true })
      const installerError = asInstallerError(error, INSTALLER_ERROR_CODES.CONNECTION_LOST, 'Windpeek did not reconnect yet.')
      update({ phase: 'reconnect', error: installerError, safeToDisconnect: true })
      reportFailure(installerError, 'reconnect')
      return state
    }
  }

  async function submitWifi({ ssid, password }) {
    if (state.phase !== 'wifi') return state
    resetDiagnosticDelivery()
    const currentAttempt = attempt
    const credentials = { ssid: String(ssid), password: String(password) }
    let releaseCredentialLock = () => {}
    try { releaseCredentialLock = diagnostics.acquireCredentialLock?.(credentials) ?? releaseCredentialLock } catch {}
    try {
      if (!await configure(credentials, currentAttempt)) return state
      completeAttempt()
    } catch (error) {
      if (currentAttempt !== attempt) return state
      const installerError = asInstallerError(error, INSTALLER_ERROR_CODES.WIFI_FAILED, 'Windpeek could not connect to that Wi-Fi network.')
      const phase = installerError.code === INSTALLER_ERROR_CODES.WIFI_FAILED
        ? 'wifi'
        : installerError.code === INSTALLER_ERROR_CODES.CONNECTION_LOST ? 'reconnect' : 'error'
      update({ phase, error: installerError, safeToDisconnect: true })
      reportFailure(installerError, phase)
    } finally {
      credentials.ssid = ''
      credentials.password = ''
      try { releaseCredentialLock() } catch {}
      flushPendingFailures()
    }
    return state
  }

  async function performCancellation() {
    if (!state.safeToDisconnect) return state
    attempt += 1
    confirmed = false
    awaitingWrittenFirmware = false
    operationController?.abort()
    operationController = null
    await releaseConnections({ clearDevice: true })
    port = null
    pendingFailures.length = 0
    latestDiagnosticOccurrence = null
    try { diagnostics.destroy?.() } catch {}
    update({ ...INITIAL_STATE })
    return state
  }

  async function cancel() {
    if (cancellationPromise) return cancellationPromise
    cancellationPromise = performCancellation()
    try {
      return await cancellationPromise
    } finally {
      cancellationPromise = null
    }
  }

  return {
    getState: () => state,
    subscribe(listener) { listeners.add(listener); listener(state); return () => listeners.delete(listener) },
    connect,
    confirmDevice,
    scanNetworks,
    submitWifi,
    attachReconnectedPort,
    reconnect,
    cancel,
  }
}
