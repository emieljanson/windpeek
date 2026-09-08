import { describe, expect, it } from 'vitest'
import {
  createInstallerDemoSession,
  INSTALLER_DEMO_FIRMWARE_DURATION_MS,
  INSTALLER_DEMO_RECONNECTING_DURATION_MS,
} from '../../src/installer/createInstallerDemoSession'

describe('installer demo session', () => {
  const immediateClock = () => {
    let time = 0
    return {
      now: () => time,
      waitFor: (duration) => {
        time += duration
        return Promise.resolve()
      },
    }
  }

  it('walks the complete happy path without browser USB or firmware access', async () => {
    const phases = []
    const session = createInstallerDemoSession(immediateClock())
    session.subscribe((state) => phases.push(state.phase))

    expect(session.isDemo).toBe(true)
    await session.connect()
    expect(phases).toEqual(['ready', 'choosing-device', 'checking-device', 'confirm-device'])

    await session.confirmDevice()
    const distinctInstallPhases = phases.filter((phase, index) => phase !== phases[index - 1])
    expect(distinctInstallPhases.slice(-4)).toEqual(['downloading', 'installing-firmware', 'reconnecting', 'wifi'])
    expect(phases.at(-1)).toBe('wifi')
    await expect(session.scanNetworks()).resolves.toContainEqual(expect.objectContaining({ ssid: 'Windpeek Studio' }))
    await expect(session.scanNetworks()).resolves.toContainEqual(expect.objectContaining({
      ssid: 'North Sea Guest',
      secured: false,
    }))

    await session.submitWifi({ ssid: 'Windpeek Studio', password: 'demo-only' })
    const distinctWifiPhases = phases.filter((phase, index) => phase !== phases[index - 1])
    expect(distinctWifiPhases.slice(-3)).toEqual(['configuring', 'verifying', 'complete'])
  })

  it('holds the firmware-writing scene long enough for its camera and cable reveal', async () => {
    const waits = []
    let time = 0
    const session = createInstallerDemoSession({
      now: () => time,
      waitFor: (duration) => {
        waits.push(duration)
        time += duration
        return Promise.resolve()
      },
    })
    const firmwareProgress = []
    session.subscribe((state) => {
      if (state.phase === 'installing-firmware') firmwareProgress.push(state.progress)
    })
    await session.confirmDevice()

    expect(INSTALLER_DEMO_FIRMWARE_DURATION_MS).toBe(3000)
    expect(waits.reduce((total, duration) => total + duration, 0))
      .toBe(650 + INSTALLER_DEMO_FIRMWARE_DURATION_MS + INSTALLER_DEMO_RECONNECTING_DURATION_MS)
    expect(firmwareProgress.length).toBeGreaterThan(20)
    expect(firmwareProgress.at(0)).toBeCloseTo(0.18)
    expect(firmwareProgress.at(-1)).toBeCloseTo(0.78)
    expect(firmwareProgress.every((progress, index) => index === 0 || progress >= firmwareProgress[index - 1])).toBe(true)
  })

  it('keeps both post-WiFi steps readable for at least two seconds', async () => {
    let currentPhase = 'ready'
    let time = 0
    const waitsByPhase = {}
    const session = createInstallerDemoSession({
      now: () => time,
      waitFor: (duration) => {
        waitsByPhase[currentPhase] = (waitsByPhase[currentPhase] ?? 0) + duration
        time += duration
        return Promise.resolve()
      },
    })
    session.subscribe((state) => { currentPhase = state.phase })

    await session.submitWifi({ ssid: 'North Sea Guest', password: '' })

    expect(waitsByPhase.configuring).toBeGreaterThanOrEqual(2000)
    expect(waitsByPhase.verifying).toBeGreaterThanOrEqual(2000)
  })
})
