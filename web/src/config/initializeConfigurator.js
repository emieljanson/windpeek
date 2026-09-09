import { siteVariant, siteDisplayDefaults } from '../marketing/siteVariant'
import { persistConfigurator } from './configuratorPreferences'
import { applyConfigurationUrl, syncConfigurationUrl } from './configurationUrl'
import { availableStorage } from '../storage'
import { isConfiguratorLocation } from '../routes'

export function initializeConfigurator(context, { browser = window, storage = availableStorage() } = {}) {
  const { store } = context
  if (store.$id !== 'configurator') return
  const params = new URLSearchParams(browser.location.search)
  const preview = params.has('devicePreview') || params.has('installerDemo')
  const configuring = isConfiguratorLocation(browser.location) && !preview
  store.$patch(siteDisplayDefaults(siteVariant(browser.location)))
  // Restore first without subscribing, so a shared link cannot persist a half-applied draft.
  if (!preview) persistConfigurator(context, storage, { subscribe: false })
  if (configuring) {
    if (!params.has('cfg') && params.get('swell') === '1') store.$patch({ windSize: 'small', swellSize: 'large' })
    applyConfigurationUrl(store, browser.location.search, storage)
  }
  store.swellFocus = store.swellSize !== 'off'
  if (configuring) {
    // Persistence subscription only; restoring a second time would overwrite the shared link.
    persistConfigurator(context, storage, { restore: false })
    syncConfigurationUrl(store, browser)
  }
}
