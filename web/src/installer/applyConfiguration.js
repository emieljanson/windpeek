import { InstallerError, INSTALLER_ERROR_CODES } from './installerErrors'

// Includes the device's failed candidate connection and restoration of saved Wi-Fi.
const WIFI_TIMEOUT_MS = 105000
const APPLY_TIMEOUT_MS = 120000
const MAX_STATUS_POLLS = 180
const MAX_APPLY_ATTEMPTS = 2

function verified(status, digest) {
  return status.configurationDigest === digest && status.wifi === 'connected' && status.render === 'valid'
}

function retryableForecastFailure(status) {
  if (status.apply !== 'render_failed' || status.allocationFailed || status.responseTooLarge) return false
  // Do not hammer a rate-limited API or retry invalid settings/parsed data.
  if (status.httpStatus >= 400) return status.httpStatus === 408 || status.httpStatus >= 500
  return Number.isInteger(status.transportError) && status.transportError !== 0
}

function verificationError(message = 'The device could not apply these settings.') {
  return new InstallerError(INSTALLER_ERROR_CODES.VERIFICATION_FAILED, message)
}

// One setup transaction owns credentials through verification and any bounded
// retry. A reconnected browser observes a running transaction before mutating it.
export async function applyConfiguration({
  protocol, configuration, credentials, applying = false, completionAck = false, isCurrent, update, waitFor, now, recordFailure, recordRetry, recoverConnection,
}) {
  let recoveredConnection = false
  async function recover(error) {
    if (recoveredConnection || !recoverConnection || !isCurrent() ||
        ![INSTALLER_ERROR_CODES.CONNECTION_LOST, INSTALLER_ERROR_CODES.INVALID_RESPONSE].includes(error?.code)) throw error
    recoveredConnection = true
    protocol = await recoverConnection(error)
    if (!protocol) throw error
  }

  async function readStatus(timeout) {
    try { return await protocol.request('get_state', undefined, timeout) }
    catch (error) {
      await recover(error)
      if (!isCurrent()) return null
      return protocol.request('get_state', undefined, timeout)
    }
  }

  async function finish() {
    if (completionAck) {
      // Verification already proved the saved setup and panel. A lost cleanup
      // acknowledgement must not turn that success into an installation error.
      try { await protocol.request('finish_setup') } catch {}
    }
    return isCurrent()
  }

  async function waitForApply(acknowledgement) {
    const deadline = now() + 240000
    for (let poll = 0; poll < MAX_STATUS_POLLS; poll += 1) {
      if (poll > 0 || acknowledgement === 'applying') await waitFor(1000)
      if (!isCurrent()) return null
      // After a suspended tab wakes, read the terminal state once before
      // declaring a deadline failure: the device may have finished hours ago.
      const remaining = deadline - now()
      const timeout = remaining > 0 ? Math.min(APPLY_TIMEOUT_MS, remaining) : 15000
      const status = await readStatus(timeout)
      if (!isCurrent()) return null
      if (['render_failed', 'commit_failed'].includes(status.apply)) return status
      if (status.apply === 'complete' ||
          (acknowledgement === 'complete' && [undefined, 'idle'].includes(status.apply))) return status
      if (now() >= deadline) break
    }
    throw verificationError('The device is taking longer than expected.')
  }

  let firstAttempt = 0
  if (applying) {
    update({ phase: 'verifying', progress: 0.92, safeToDisconnect: true })
    const status = await waitForApply('applying')
    if (!status) return false
    if (!credentials && status.apply === 'complete' && verified(status, configuration.digest)) return finish()
    if (['render_failed', 'commit_failed'].includes(status.apply)) {
      recordFailure(status)
      if (!retryableForecastFailure(status)) throw verificationError()
      firstAttempt = 1
      recordRetry(firstAttempt)
      await waitFor(3000)
    }
  }

  for (let attempt = firstAttempt; attempt < MAX_APPLY_ATTEMPTS; attempt += 1) {
    if (!isCurrent()) return false
    update({ phase: 'configuring', progress: 0.82, safeToDisconnect: true })
    const unixTime = Math.floor(now() / 1000)
    if (!Number.isSafeInteger(unixTime)) {
      throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'Windpeek could not read this computer’s time.')
    }
    const begun = await protocol.request('begin', { unixTime, ...(completionAck ? { completionAck: true } : {}) })
    if (!isCurrent()) return false
    if (begun.status !== 'ready') {
      throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE,
        begun.status === 'clock_rejected' ? 'Windpeek could not set its clock from this computer.'
          : 'Device not ready. Reconnect to check progress.')
    }
    const staged = await protocol.request('stage_configuration', { configuration })
    if (!isCurrent()) return false
    if (staged.status !== 'configuration_staged') {
      throw new InstallerError(INSTALLER_ERROR_CODES.INVALID_RESPONSE, 'Windpeek rejected this configuration.')
    }
    if (credentials) {
      const wifi = await protocol.request('test_wifi', credentials, WIFI_TIMEOUT_MS)
      if (!isCurrent()) return false
      if (wifi.status !== 'wifi_ready') {
        throw new InstallerError(INSTALLER_ERROR_CODES.WIFI_FAILED, 'Could not connect. Check the network and password.')
      }
    }
    update({ phase: 'verifying', progress: 0.92 })
    let applied
    try { applied = await protocol.request('apply_configuration', undefined, APPLY_TIMEOUT_MS) }
    catch (error) {
      // A lost acknowledgement does not prove the write failed. Observe it;
      // never repeat this mutation just because its response was lost.
      await recover(error)
      applied = { status: 'applying' }
    }
    if (!isCurrent()) return false
    if (!['applying', 'complete'].includes(applied.status)) {
      recordFailure({ apply: applied.status })
      throw verificationError()
    }
    const status = await waitForApply(applied.status)
    if (!status) return false
    const failed = ['render_failed', 'commit_failed'].includes(status.apply)
    if (!failed && verified(status, configuration.digest)) return finish()
    recordFailure(status)
    if (status.apply === 'complete' && status.render !== 'valid') {
      throw verificationError('Settings saved, but the forecast screen could not be verified.')
    }
    if (!retryableForecastFailure(status) || attempt + 1 === MAX_APPLY_ATTEMPTS) throw verificationError()
    recordRetry(attempt + 1)
    await waitFor(3000)
  }
  return false
}
