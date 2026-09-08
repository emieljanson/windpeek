import { describe, expect, it } from 'vitest'
import { INSTALL_ACTIONS, resolveInstallAction } from '../../src/installer/actionResolver'

const release = { boardId: 'seeedstudio_reterminal_e1002', chipFamily: 'ESP32-S3', firmwareLayoutVersion: 1, version: '2.0.0' }
const current = {
  kind: 'windpeek',
  verifiedBoard: true,
  boardId: release.boardId,
  chipFamily: release.chipFamily,
  firmwareVersion: release.version,
  configurationDigest: 'wanted',
  wifiHealthy: true,
}

describe('installer action resolver', () => {
  it('does nothing when firmware, configuration and Wi-Fi are current', () => {
    expect(resolveInstallAction({ device: current, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.UP_TO_DATE)
  })

  it('updates configuration without flashing when setup differs', () => {
    expect(resolveInstallAction({ device: { ...current, configurationDigest: 'old' }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.UPDATE_CONFIGURATION)
    expect(resolveInstallAction({ device: { ...current, wifiHealthy: false }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.UPDATE_CONFIGURATION)
  })

  it('chooses firmware update, install and reinstall automatically', () => {
    expect(resolveInstallAction({ device: { ...current, firmwareVersion: '1.0.0' }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.UPDATE_FIRMWARE)
    expect(resolveInstallAction({ device: { ...current, firmwareVersion: null }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.INSTALL)
    expect(resolveInstallAction({ device: { ...current, damaged: true }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.REINSTALL)
    expect(resolveInstallAction({ device: { ...current, firmwareLayoutVersion: 2 }, release, configurationDigest: 'wanted' })).toEqual({ action: INSTALL_ACTIONS.REINSTALL, reason: 'flash-layout-changed' })
  })

  it('updates firmware to migrate an older supported configuration version', () => {
    expect(resolveInstallAction({
      device: { ...current, configurationVersion: 2 },
      release,
      configurationDigest: 'wanted',
      requiredConfigurationVersion: 3,
    })).toEqual({ action: INSTALL_ACTIONS.UPDATE_FIRMWARE, reason: 'configuration-version-outdated' })
  })

  it('requires enclosure confirmation for an otherwise compatible ESP32-S3', () => {
    expect(resolveInstallAction({ device: { kind: 'rom', verifiedBoard: false, chipFamily: 'ESP32-S3' }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.CONFIRM_E1002)
  })

  it('blocks known different boards and chips', () => {
    expect(resolveInstallAction({ device: { ...current, boardId: 'seeedstudio_reterminal_e1001' }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.BLOCKED)
    expect(resolveInstallAction({ device: { ...current, hardwareModelMismatch: true }, release, configurationDigest: 'wanted' })).toEqual({ action: INSTALL_ACTIONS.BLOCKED, reason: 'different-windpeek-model' })
    expect(resolveInstallAction({ device: { kind: 'rom', chipFamily: 'ESP32-C3' }, release, configurationDigest: 'wanted' }).action).toBe(INSTALL_ACTIONS.BLOCKED)
  })
})
