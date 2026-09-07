import { describe, expect, it } from 'vitest'
import { isConfiguratorLocation, pageBackgroundForLocation } from '../src/routes'

describe('Windscout entry route', () => {
  it('shows the landing page at the root', () => {
    expect(isConfiguratorLocation({ search: '' })).toBe(false)
  })

  it.each(['?configure', '?devicePreview=seeedstudio_reterminal_e1002', '?installerDemo=1'])(
    'opens the configurator for %s',
    (search) => expect(isConfiguratorLocation({ search })).toBe(true),
  )

  it('uses the landing surface for Safari chrome only on the landing page', () => {
    expect(pageBackgroundForLocation({ search: '' })).toBe('#ffffff')
    expect(pageBackgroundForLocation({ search: '?configure' })).toBe('#f3f5f7')
  })
})
