export const INSTALL_ACTIONS = Object.freeze({
  INSTALL: 'install',
  REINSTALL: 'reinstall',
  UPDATE_FIRMWARE: 'update-firmware',
  UPDATE_CONFIGURATION: 'update-configuration',
  UP_TO_DATE: 'up-to-date',
  CONFIRM_E1002: 'confirm-e1002',
  BLOCKED: 'blocked',
})

export function resolveInstallAction({
  device,
  release,
  configurationDigest,
  requiredConfigurationVersion,
}) {
  if (!device || device.kind === 'unknown') {
    return { action: INSTALL_ACTIONS.BLOCKED, reason: 'device-not-recognized' }
  }
  if (device.hardwareModelMismatch) {
    return { action: INSTALL_ACTIONS.BLOCKED, reason: 'different-windpeek-model' }
  }
  if (device.boardId && device.boardId !== release.boardId) {
    return { action: INSTALL_ACTIONS.BLOCKED, reason: 'different-windpeek-model' }
  }
  if (device.chipFamily && device.chipFamily !== release.chipFamily) {
    return { action: INSTALL_ACTIONS.BLOCKED, reason: 'incompatible-chip' }
  }
  if (!device.verifiedBoard && device.chipFamily === release.chipFamily) {
    return { action: INSTALL_ACTIONS.CONFIRM_E1002, reason: 'enclosure-not-verifiable' }
  }
  if (device.damaged) {
    return { action: INSTALL_ACTIONS.REINSTALL, reason: 'firmware-damaged' }
  }
  if ((device.firmwareLayoutVersion ?? 1) !== (release.firmwareLayoutVersion ?? 1)) {
    return { action: INSTALL_ACTIONS.REINSTALL, reason: 'flash-layout-changed' }
  }
  if (!device.firmwareVersion) {
    return { action: INSTALL_ACTIONS.INSTALL, reason: 'windpeek-not-installed' }
  }
  if (Number.isInteger(requiredConfigurationVersion) &&
      Number.isInteger(device.configurationVersion) &&
      device.configurationVersion < requiredConfigurationVersion) {
    return { action: INSTALL_ACTIONS.UPDATE_FIRMWARE, reason: 'configuration-version-outdated' }
  }
  if (device.firmwareVersion !== release.version) {
    return { action: INSTALL_ACTIONS.UPDATE_FIRMWARE, reason: 'firmware-outdated' }
  }
  if (device.configurationDigest !== configurationDigest || !device.wifiHealthy) {
    return { action: INSTALL_ACTIONS.UPDATE_CONFIGURATION, reason: 'setup-differs' }
  }
  return { action: INSTALL_ACTIONS.UP_TO_DATE, reason: 'already-current' }
}
