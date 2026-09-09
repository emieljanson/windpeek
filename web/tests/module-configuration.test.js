import { describe, it, expect } from 'vitest'
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { useConfiguratorStore } from '../src/stores/configurator'
import { persistConfigurator } from '../src/config/configuratorPreferences'
import { createInstalledConfiguration, createDefaultDisplayConfiguration, installedConfigurationDigest, validateInstalledConfiguration } from '../src/config/configuration'
import { MODULE_IDS } from '../src/config/modules'
import { SWELL_MODELS, fetchOpenMeteoSwell } from '../src/forecast/openMeteoSwell'

function createStore(storage) {
  const pinia = createPinia().use(context => persistConfigurator(context, storage))
  createApp({}).use(pinia)
  return useConfiguratorStore(pinia)
}

describe('module configuration', () => {
  it('restores sizes and order and keeps an off module in its position', () => {
    let saved = null
    const storage = { getItem: () => saved, setItem: (_key, value) => { saved = value } }
    const store = createStore(storage)
    store.setModuleSize('wind', 'small')
    store.moveModule('tide', 0)
    store.setModuleSize('wind', 'off')
    store.setShowDedicatedFooter(true)
    const restored = createStore(storage)
    expect(restored.moduleOrder).toEqual(['tide', 'wind', 'swell', 'weather', 'temperature'])
    expect(restored.windSize).toBe('off')
    expect(restored.showDedicatedFooter).toBe(true)
    expect(restored.setModuleOrder(['wind', 'wind'])).toBe(false)
    expect(restored.moveModule('tide', -1)).toBe(false)
  })
  it('restores Weather Large and an explicit unit without splitting its rows', () => {
    let saved = null
    const storage = { getItem: () => saved, setItem: (_key, value) => { saved = value } }
    const store = createStore(storage)
    store.setWeatherSize('large')
    store.setTemperatureUnit('fahrenheit')
    const restored = createStore(storage)
    expect(restored.weatherSize).toBe('large')
    expect(restored.temperatureUnit).toBe('fahrenheit')
    expect(restored.temperatureUnitInitialized).toBe(true)
    expect(restored.moduleOrder.indexOf('temperature')).toBe(restored.moduleOrder.indexOf('weather') + 1)
  })
  it('uses IP country once without changing the selected spot or an explicit unit', async () => {
    const store = createStore(null)
    store.markUserSpotIntent()
    await store.initializeNearbyDefault({ locationFetcher: async () => ({ countryCode: 'US', latitude: 40, longitude: -74 }) })
    expect(store.temperatureUnit).toBe('fahrenheit')
    expect(store.selectedSpotId).toBe('brouwersdam')
    const manual = createStore(null)
    manual.markUserSpotIntent()
    let resolve
    const pending = manual.initializeNearbyDefault({ locationFetcher: () => new Promise(done => { resolve = done }) })
    manual.setTemperatureUnit('celsius')
    resolve({ countryCode: 'US', latitude: 40, longitude: -74 })
    await pending
    expect(manual.temperatureUnit).toBe('celsius')
  })
  it('restores temperature without enabling weather', () => {
    const store = createStore({ getItem: () => JSON.stringify({ showWeather: false, showTemperature: true, moduleOrder: ['temperature', 'wind', 'tide', 'weather', 'swell'] }), setItem() {} })
    expect(store.weatherSize).toBe('large')
    expect(store.showWeather).toBe(false)
    expect(store.moduleOrder).toEqual(['wind', 'tide', 'weather', 'temperature', 'swell'])
  })
  it('ignores corrupt saved preferences', () => {
    const store = createStore({ getItem: () => JSON.stringify({ moduleOrder: ['tide'], windSize: 'huge' }), setItem() {} })
    expect(store.moduleOrder).toEqual(MODULE_IDS)
    expect(store.windSize).toBe('large')
  })
  it('includes independent models, sizes and order in the installed digest', () => {
    const configuration = createInstalledConfiguration({
      spot: { id: 'coast', name: 'Coast', latitude: 52, longitude: 4, timezone: 'Europe/Amsterdam' },
      modelId: 'knmi_harmonie',
      display: { ...createDefaultDisplayConfiguration(), windSize: 'small', swellSize: 'large', swellModel: 'meteofrance_wave', moduleOrder: ['swell', 'tide', 'wind', 'weather', 'temperature'] },
    })
    expect(validateInstalledConfiguration(JSON.parse(JSON.stringify(configuration)))).toBe(true)
    const edited = structuredClone(configuration)
    edited.display.moduleOrder.reverse()
    expect(installedConfigurationDigest(edited)).not.toBe(configuration.digest)
    expect(validateInstalledConfiguration(edited)).toBe(false)
  })
  it('rejects models that cannot provide our required swell fields', async () => {
    expect(SWELL_MODELS.map(model => model.value)).toEqual(['best_match', 'meteofrance_wave', 'ncep_gfswave025', 'dwd_ewam'])
    await expect(fetchOpenMeteoSwell({}, { model: 'ecmwf_wam' })).rejects.toThrow('Unknown swell model')
  })
})
