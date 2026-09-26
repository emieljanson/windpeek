// @vitest-environment node
import { describe, expect, it, vi } from 'vitest'
import { createInstallerSuccessReporter } from '../../src/installer/installerSuccessReporting'

describe('installer success reporting', () => {
  it('sends only public installation metadata to the existing relay', async () => {
    const fetchImpl = vi.fn(async () => new Response(null, { status: 204 }))
    const report = createInstallerSuccessReporter({ endpoint: 'https://reports.example/report', fetchImpl })
    await report({ action: 'install', boardId: 'seeedstudio_reterminal_e1002', password: 'private' })
    const [url, options] = fetchImpl.mock.calls[0]
    expect(url).toBe('https://reports.example/success')
    expect(JSON.parse(options.body)).toEqual({
      eventId: expect.any(String), action: 'install', boardId: 'seeedstudio_reterminal_e1002',
    })
    expect(options.credentials).toBe('omit')
    expect(options.keepalive).toBe(true)
  })

  it('does not send settings changes, checks, or unconfigured reports', async () => {
    const fetchImpl = vi.fn()
    for (const action of ['up-to-date', 'update-configuration', undefined]) {
      await createInstallerSuccessReporter({ endpoint: 'https://reports.example/report', fetchImpl })({ action })
    }
    await createInstallerSuccessReporter({ endpoint: '', fetchImpl })({ action: 'install' })
    expect(fetchImpl).not.toHaveBeenCalled()
  })

  it('absorbs network failures without retrying an uncertain delivery', async () => {
    const fetchImpl = vi.fn(async () => { throw new Error('offline') })
    await expect(createInstallerSuccessReporter({ endpoint: 'https://reports.example/report', fetchImpl })({ action: 'install' })).resolves.toBeUndefined()
    expect(fetchImpl).toHaveBeenCalledOnce()
  })
})
