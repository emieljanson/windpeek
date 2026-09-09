import { describe, it, expect } from 'vitest'
import { createApp, nextTick } from 'vue'
import { createPinia } from 'pinia'
import { useConfiguratorStore } from '../src/stores/configurator'
import { configurationUrl, readConfigurationUrl, applyConfigurationUrl } from '../src/config/configurationUrl'
import { initializeConfigurator } from '../src/config/initializeConfigurator'
import { siteVariant, siteDisplayDefaults, configuratorLink } from '../src/marketing/siteVariant'
import { createPersonalSpot } from '../src/spots/personalSpots'

function browserAt(href) {
  const browser = { location: new URL(href) }
  browser.history = { state: { keep: true }, replaceState(state, _, url) { browser.location = new URL(url); browser.history.state = state } }
  return browser
}
function storageWith(saved = null) {
  const data = new Map(saved ? [['windpeek-configurator-v1', JSON.stringify(saved)]] : [])
  return { getItem: key => data.get(key) ?? null, setItem: (key, value) => data.set(key, value) }
}
function storeWith(browser, storage = null) {
  const pinia = createPinia()
  if (browser) pinia.use(context => initializeConfigurator(context, { browser, storage }))
  createApp({}).use(pinia)
  return useConfiguratorStore(pinia)
}

describe('site entry and share URLs', () => {
  it('uses hostname in production and a local preview flag', () => {
    expect(siteVariant(new URL('https://swellpeek.com/')).id).toBe('swell')
    expect(siteVariant(new URL('https://windpeek.com/?site=swell')).id).toBe('wind')
    expect(siteVariant(new URL('http://localhost:4174/?site=swell')).id).toBe('swell')
    expect(configuratorLink(new URL('http://127.0.0.1:4174/?site=swell'))).toContain('site=swell')
  })
  it('starts each configurator with the same layout as its landing', () => {
    for (const site of ['wind', 'swell']) {
      const browser = browserAt(`http://localhost/?configure&site=${site}`)
      const store = storeWith(browser)
      expect(store.$state).toMatchObject(siteDisplayDefaults(siteVariant(browser.location)))
      expect(store.swellFocus).toBe(site === 'swell')
      expect(browser.location.searchParams.get('cfg')).toBe('1')
    }
  })
  it('round-trips all display choices, independent models, device and order', () => {
    const source = storeWith()
    source.$patch({ windSize: 'off', swellSize: 'large', showWeather: false, showTemperature: true,
      showTide: true, showDedicatedFooter: true, showThreshold: true, threshold: 23,
      selectedBoardId: 'seeedstudio_reterminal_e1003', selectedModelId: 'knmi_harmonie',
      selectedSwellModelId: 'dwd_ewam', temperatureUnit: 'fahrenheit', timeFormat: '12-hour',
      moduleOrder: ['tide', 'temperature', 'swell', 'wind', 'weather'] })
    const url = configurationUrl(source, 'http://localhost/?configure&site=swell&utm_source=test')
    const decoded = readConfigurationUrl(url.search, source.spots)
    expect(decoded).not.toBeNull()
    for (const [key, value] of Object.entries(decoded.patch)) {
      if (key !== 'hasUserSpotIntent' && key !== 'temperatureUnitInitialized') expect(source[key]).toEqual(value)
    }
    expect(url.searchParams.get('utm_source')).toBe('test')
    expect(url.searchParams.has('wifi')).toBe(false)
  })
  it('shared links beat storage, and storage beats site defaults', async () => {
    const storage = storageWith({ windSize: 'off', swellSize: 'off', showWeather: false })
    const saved = storeWith(browserAt('http://localhost/?configure&site=swell'), storage)
    expect(saved.windSize).toBe('off')
    expect(saved.swellSize).toBe('off')
    const source = storeWith()
    source.$patch({ windSize: 'small', swellSize: 'large', moduleOrder: ['swell', 'tide', 'temperature', 'wind', 'weather'] })
    const browser = browserAt(configurationUrl(source, 'http://localhost/?configure').href)
    const restored = storeWith(browser, storage)
    expect(restored.windSize).toBe('small')
    expect(restored.swellSize).toBe('large')
    expect(restored.showWeather).toBe(true)
    restored.threshold = 27
    await nextTick()
    expect(browser.location.searchParams.get('minimum')).toBe('27')
    expect(browser.history.state).toEqual({ keep: true })
    const again = storeWith(browserAt('http://localhost/?configure'), storage)
    expect(again.moduleOrder).toEqual(source.moduleOrder)
  })
  it('does not let IP location replace the shared spot or unit', async () => {
    const source = storeWith()
    const browser = browserAt(configurationUrl(source, 'http://localhost/?configure').href)
    const restored = storeWith(browser)
    await restored.initializeNearbyDefault({ locationFetcher: async () => ({ countryCode: 'US', latitude: 40, longitude: -74 }) })
    expect(restored.selectedSpotId).toBe(source.selectedSpotId)
    expect(restored.temperatureUnit).toBe(source.temperatureUnit)
  })
  it('shares custom spots without requiring storage and preserves Unicode names', () => {
    const source = storeWith()
    const spot = createPersonalSpot({ name: 'Côte & plage', latitude: 43.5, longitude: -1.5, timezone: 'Europe/Paris', countryCode: 'fr' })
    source.personalSpots = [spot]
    source.selectedSpotId = spot.id
    source.selectedModelId = 'meteofrance_arome'
    const url = configurationUrl(source, 'https://swellpeek.com/?configure')
    const target = storeWith()
    expect(applyConfigurationUrl(target, url.search, null)).toBe(true)
    expect(target.spotById(target.selectedSpotId)).toEqual(spot)
    expect(target.selectedModelId).toBe('meteofrance_arome')
  })
  it('rejects malformed or unsupported links atomically', () => {
    const source = storeWith()
    const original = configurationUrl(source, 'https://windpeek.com/?configure')
    for (const [key, value] of [['cfg', '2'], ['wind', '__proto__'], ['waves', 'huge'], ['board', '__proto__'], ['order', 'wind,wind'], ['minimum', '-1'], ['minimum', '100'], ['time', 'nope'], ['unit', 'kelvin'], ['spot', 'missing'], ['wind-model', 'noaa_hrrr'], ['weather', 'yes']]) {
      const url = new URL(original)
      url.searchParams.set(key, value)
      expect(readConfigurationUrl(url.search, source.spots), key).toBeNull()
    }
    const duplicate = new URL(original)
    duplicate.searchParams.append('wind', 'hide')
    expect(readConfigurationUrl(duplicate.search, source.spots)).toBeNull()
  })
  it('landing visits do not overwrite drafts or add configuration URL fields', async () => {
    const storage = storageWith({ windSize: 'off' })
    const before = storage.getItem('windpeek-configurator-v1')
    const browser = browserAt('http://localhost/?site=swell')
    const store = storeWith(browser, storage)
    store.forecastRevision++
    store.showWeather = false
    await nextTick()
    expect(storage.getItem('windpeek-configurator-v1')).toBe(before)
    expect(browser.location.search).toBe('?site=swell')
  })
})
