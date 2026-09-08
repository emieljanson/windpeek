const DEMO_NETWORKS = [
  { ssid: 'Windpeek Studio', rssi: -35, secured: true },
  { ssid: 'North Sea Guest', rssi: -58, secured: false },
]

export const INSTALLER_DEMO_FIRMWARE_DURATION_MS = 3000
export const INSTALLER_DEMO_RECONNECTING_DURATION_MS = 800
const DEMO_PROGRESS_TICK_MS = 100
const DEMO_CONFIGURING_DURATION_MS = 2000
const DEMO_VERIFYING_DURATION_MS = 2000

export function createInstallerDemoSession({
  waitFor = (duration) => new Promise((resolve) => window.setTimeout(resolve, duration)),
  now = () => performance.now(),
} = {}) {
  let state = { phase: 'ready', progress: 0, safeToDisconnect: true, error: null }
  let attempt = 0
  const listeners = new Set()
  const update = (patch) => {
    state = { ...state, ...patch }
    listeners.forEach((listener) => listener(state))
  }
  const pause = async (duration, expectedAttempt) => {
    await waitFor(duration)
    return attempt === expectedAttempt
  }
  const advanceProgress = async ({ from, to, duration, expectedAttempt }) => {
    const startedAt = now()
    while (true) {
      const elapsed = Math.max(0, now() - startedAt)
      if (elapsed >= duration) {
        update({ progress: to })
        return true
      }
      if (!await pause(Math.min(DEMO_PROGRESS_TICK_MS, duration - elapsed), expectedAttempt)) return false
      const ratio = Math.min(1, Math.max(0, (now() - startedAt) / duration))
      update({ progress: from + (to - from) * ratio })
    }
  }
  const installFreshDevice = async () => {
    const currentAttempt = ++attempt
    update({ phase: 'downloading', progress: 0.08, safeToDisconnect: true })
    if (!await advanceProgress({ from: 0.08, to: 0.18, duration: 650, expectedAttempt: currentAttempt })) return state
    update({ phase: 'installing-firmware', progress: 0.18, safeToDisconnect: false })
    if (!await advanceProgress({
      from: 0.18,
      to: 0.78,
      duration: INSTALLER_DEMO_FIRMWARE_DURATION_MS,
      expectedAttempt: currentAttempt,
    })) return state
    update({ phase: 'reconnecting', progress: 0.8, safeToDisconnect: true })
    if (!await pause(INSTALLER_DEMO_RECONNECTING_DURATION_MS, currentAttempt)) return state
    update({ phase: 'wifi', progress: 0.82, safeToDisconnect: true })
    return state
  }

  return {
    isDemo: true,
    subscribe(listener) {
      listeners.add(listener)
      listener(state)
      return () => listeners.delete(listener)
    },
    async connect() {
      const currentAttempt = ++attempt
      update({ phase: 'choosing-device', progress: 0, safeToDisconnect: true, error: null })
      if (!await pause(550, currentAttempt)) return state
      update({ phase: 'checking-device', progress: 0.02 })
      if (!await pause(1000, currentAttempt)) return state
      update({ phase: 'confirm-device', progress: 0, action: { action: 'confirm-e1002', reason: 'demo' } })
      return state
    },
    confirmDevice: installFreshDevice,
    async reconnect() {
      update({ phase: 'wifi', progress: 0.82, safeToDisconnect: true, error: null })
      return state
    },
    async scanNetworks() {
      return DEMO_NETWORKS
    },
    async submitWifi() {
      const currentAttempt = ++attempt
      update({ phase: 'configuring', progress: 0.82, safeToDisconnect: true })
      if (!await advanceProgress({ from: 0.82, to: 0.9, duration: DEMO_CONFIGURING_DURATION_MS, expectedAttempt: currentAttempt })) return state
      update({ phase: 'verifying', progress: 0.9 })
      if (!await advanceProgress({ from: 0.9, to: 0.99, duration: DEMO_VERIFYING_DURATION_MS, expectedAttempt: currentAttempt })) return state
      update({ phase: 'complete', progress: 1 })
      return state
    },
    async cancel() {
      attempt += 1
      state = { phase: 'ready', progress: 0, safeToDisconnect: true, error: null }
      return state
    },
  }
}
