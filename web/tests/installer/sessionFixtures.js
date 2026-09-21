import { vi } from 'vitest'
import { CONFIGURATION_VERSION } from '../../src/config/configuration'

export const configuration = { digest: 'wanted' }
export const release = { manifest: { version: '2.0.0', boardId: 'seeedstudio_reterminal_e1002', chipFamily: 'ESP32-S3', firmwareLayoutVersion: 1 }, manifestUrl: new URL('https://example.test/manifest.json') }


export function appProtocol(state = {}) {
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
          ...(state.completionAck ? ['completion-ack'] : []),
        ],
        ...(state.hardwareModel ? {
          hardwareModel: state.hardwareModel,
          hardwareProfileRevision: state.hardwareProfileRevision ?? 0,
        } : {}),
      }
      if (command === 'get_state') return { configurationDigest: activeDigest, wifi: state.wifiHealthy === false ? 'disconnected' : 'connected', render: 'valid' }
      if (command === 'begin') return { status: 'ready' }
      if (command === 'stage_configuration') {
        stagedDigest = values?.configuration?.digest ?? 'wanted'
        return { status: 'configuration_staged' }
      }
      if (command === 'apply_configuration') { activeDigest = stagedDigest; return { status: 'complete' } }
      return { ok: true }
    }),
  }
}
