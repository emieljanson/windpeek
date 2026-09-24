import { INSTALLER_ERROR_CODES } from './installerErrors'
import { browserContext, createDiagnosticReport, isInstallerDiagnosticReference } from './sentryReporter'

let sessionSequence = 0

export function createInstallerFailureReporting({ diagnostics, reporter, update, getAttempt, getPhase, getAction }) {
  const session = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}-${++sessionSequence}`
  const pending = []
  let occurrence = 0
  let latest = null

  function reset() {
    latest = null
    update({ diagnosticStatus: 'idle', diagnosticReference: null, diagnosticReport: null })
  }

  function setReleaseContext({ release, action, device, expectedHardwareModel }) {
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

  function sendFailure(failure) {
    latest = failure.occurrence
    let snapshot
    try {
      snapshot = diagnostics.snapshot?.()
    } catch {
      if (failure.attempt === getAttempt()) update({ diagnosticStatus: 'failed', diagnosticReference: null, diagnosticReport: null })
      return
    }
    if (!snapshot) {
      pending.push(failure)
      return
    }
    snapshot = { ...snapshot, context: { ...snapshot.context, phase: failure.phase, errorCode: failure.error?.code, attempt: failure.attempt, ...browserContext() } }
    update({ diagnosticStatus: 'sending', diagnosticReference: null,
      diagnosticReport: createDiagnosticReport(snapshot) })
    function delivered(result) {
      if (failure.attempt !== getAttempt() || latest !== failure.occurrence) return
      const sent = result?.status === 'sent' && isInstallerDiagnosticReference(result.reference)
      update({ diagnosticStatus: sent ? 'sent' : result?.status === 'queued' ? 'queued' : 'failed',
        diagnosticReference: sent ? result.reference : null })
    }
    let report
    try { report = reporter.report({ ...failure, snapshot }, delivered) }
    catch (error) { report = Promise.reject(error) }
    Promise.resolve(report).then(delivered).catch(() => delivered({ status: 'failed' }))
  }

  function flushPending() {
    try {
      if (diagnostics.credentialsLocked) return
    } catch {
      pending.length = 0
      latest = null
      update({ diagnosticStatus: 'failed', diagnosticReference: null, diagnosticReport: null })
      return
    }
    for (const failure of pending.splice(0)) sendFailure(failure)
  }

  function report(error, phase = getPhase()) {
    if (error?.code === INSTALLER_ERROR_CODES.UNSUPPORTED) return
    const attempt = getAttempt()
    const failure = {
      attempt,
      occurrence: `${session}:${attempt}:${++occurrence}`,
      phase,
      error,
    }
    try {
      diagnostics.setContext?.({ phase, errorCode: error?.code, action: getAction()?.action, attempt })
      diagnostics.record?.({ category: 'installer', operation: phase, status: 'failed', message: error?.message })
    } catch {}
    sendFailure(failure)
  }

  function recordVerificationFailure(status) {
    try {
      diagnostics.record?.({
        category: 'verification',
        operation: 'device-state',
        deviceState: status,
        status: status?.apply ?? 'incomplete',
        message: Number.isSafeInteger(status?.applyError)
          ? `Device apply error: ${status.applyError}` : undefined,
      })
    } catch {}
  }

  return {
    reset, setReleaseContext, flushPending, report, recordVerificationFailure,
    clear() { pending.length = 0; latest = null },
    complete() { latest = null },
  }
}
