import { describe, expect, it } from 'vitest'
import { isConfiguratorLocation } from '../src/routes'

describe('Windpeek entry route', () => {
  it('shows the landing page at the root', () => {
    expect(isConfiguratorLocation({ search: '' })).toBe(false)
  })

  it.each(['?configure', '?devicePreview=seeedstudio_reterminal_e1002', '?installerDemo=1'])(
    'opens the configurator for %s',
    (search) => expect(isConfiguratorLocation({ search })).toBe(true),
  )
})
