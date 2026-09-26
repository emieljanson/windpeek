const NOTIFIABLE_ACTIONS = new Set(['install', 'reinstall', 'update-firmware'])

export function createInstallerSuccessReporter({
  endpoint = import.meta.env.VITE_INSTALLER_REPORT_URL,
  fetchImpl = (...args) => globalThis.fetch(...args),
} = {}) {
  return async ({ action, boardId }) => {
    if (!endpoint || !NOTIFIABLE_ACTIONS.has(action)) return
    try {
      await fetchImpl(new URL('/success', endpoint).href, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ eventId: crypto.randomUUID(), action, boardId }),
        credentials: 'omit', keepalive: true, redirect: 'error',
        signal: AbortSignal.timeout(5000),
      })
    } catch { /* Notifications must never affect installation. */ }
  }
}

export const reportInstallerSuccess = createInstallerSuccessReporter()
