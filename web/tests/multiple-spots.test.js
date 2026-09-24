import { beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import { useConfiguratorStore } from '../src/stores/configurator'
import { BOARD_IDS, installedConfigurationFromStore, installedConfigurationDigest, validateInstalledConfiguration } from '../src/config/configuration'
import fixture from '../../shared/configuration-fixtures/e1003-ten-spots.json'
import { configurationUrl, applyConfigurationUrl } from '../src/config/configurationUrl'
import { persistConfigurator } from '../src/config/configuratorPreferences'
import { createPersonalSpot, readPersonalSpots } from '../src/spots/personalSpots'

describe('E1003 spots', () => {
  beforeEach(() => setActivePinia(createPinia()))
  function setup() {
    const store = useConfiguratorStore()
    vi.spyOn(store, 'refreshForecast').mockResolvedValue(true)
    return store
  }
  it('replaces the previous per-spot settings when opening a different shared setup', async () => {
    const source = setup()
    const link = configurationUrl(source, 'http://localhost/?configure')
    const target = useConfiguratorStore(createPinia())
    vi.spyOn(target, 'refreshForecast').mockResolvedValue(true)
    await target.addConfiguredSpot('edam')
    target.setThreshold(30)
    await target.addConfiguredSpot('brouwersdam')
    expect(target.spotSettings.edam.threshold).toBe(30)
    expect(applyConfigurationUrl(target, link.search)).toBe(true)
    expect(target.configuredSpotIds).toEqual([])
    expect(target.spotSettings).toEqual({})
    await target.addConfiguredSpot('edam')
    expect(target.threshold).toBe(source.threshold)
  })
  it('does not retain settings for a replaced or removed active spot', async () => {
    const store = setup()
    await store.addConfiguredSpot('brouwersdam')
    await store.addConfiguredSpot('edam')
    await store.replaceConfiguredSpot('castricum-aan-zee')
    expect(store.spotSettings.edam).toBeUndefined()
    expect(store.spotSettings['castricum-aan-zee']).toBeUndefined()
    await store.removeConfiguredSpot('castricum-aan-zee')
    expect(store.spotSettings['castricum-aan-zee']).toBeUndefined()
  })
  it('imports ten personal spots in one storage write without duplicates', () => {
    const source = setup()
    source.personalSpots = Array.from({ length: 10 }, (_, index) => createPersonalSpot({
      name: `Custom beach ${index}`, latitude: 52 + index / 100,
      longitude: 5, timezone: 'Europe/Amsterdam', countryCode: 'nl',
    }))
    source.configuredSpotIds = source.personalSpots.map(spot => spot.id)
    source.selectedSpotId = source.configuredSpotIds[4]
    const link = configurationUrl(source, 'http://localhost/?configure')
    const target = useConfiguratorStore(createPinia())
    let saved
    const storage = { getItem: () => saved ?? null, setItem: vi.fn((_, value) => { saved = value }) }
    expect(applyConfigurationUrl(target, link.search, storage)).toBe(true)
    expect(storage.setItem).toHaveBeenCalledOnce()
    expect(readPersonalSpots(storage)).toHaveLength(10)
    expect(target.personalSpots).toHaveLength(10)
    expect(target.personalSpots.at(-1).id).toBe(source.selectedSpotId)
  })
  it('persists nested spot edits but does not rewrite preferences for forecast activity', () => {
    const store = setup()
    const storage = { getItem: () => null, setItem: vi.fn() }
    persistConfigurator({ store }, storage)
    store.forecastRevision++
    store.forecastMessage = 'Forecast refreshed'
    expect(storage.setItem).not.toHaveBeenCalled()
    store.spotSettings.edam = { threshold: 20 }
    store.spotSettings.edam.threshold = 24
    expect(JSON.parse(storage.setItem.mock.lastCall[1]).spotSettings.edam.threshold).toBe(24)
  })
  it('matches the ten-spot firmware fixture and detects changed settings', () => {
    expect(validateInstalledConfiguration(fixture)).toBe(true)
    expect(installedConfigurationDigest(fixture)).toBe(fixture.digest)
    const changed = structuredClone(fixture)
    changed.additionalSpots[8].display.threshold++
    expect(validateInstalledConfiguration(changed)).toBe(false)
  })
  it('sends every spot in order with its own settings, keeping v5 for one spot', async () => {
    const store = setup()
    expect(installedConfigurationFromStore(store, 'Europe/Amsterdam').version).toBe(5)
    for (const [i, spot] of store.spots.slice(0, 10).entries()) {
      await store.addConfiguredSpot(spot.id)
      store.setThreshold(17 + i)
    }
    const configuration = installedConfigurationFromStore(store, 'Europe/Amsterdam')
    expect(configuration.version).toBe(6)
    expect(configuration.additionalSpots).toHaveLength(9)
    expect([configuration, ...configuration.additionalSpots].map(c => c.spot.id)).toEqual(store.configuredSpotIds)
    expect([configuration, ...configuration.additionalSpots].map(c => c.display.threshold)).toEqual([17,18,19,20,21,22,23,24,25,26])
    expect(new TextEncoder().encode(JSON.stringify({ command: 'stage_configuration', configuration })).length).toBeLessThan(16384)
    store.setSelectedBoardId(BOARD_IDS.E1002)
    expect(installedConfigurationFromStore(store, 'Europe/Amsterdam').version).toBe(5)
  })
  it('defaults to E1003 and keeps each spot’s settings when switching', async () => {
    const store = setup()
    expect(store.selectedBoardId).toBe(BOARD_IDS.E1003)
    const [a, b] = store.spots
    await store.addConfiguredSpot(a.id)
    store.setThreshold(17)
    await store.addConfiguredSpot(b.id)
    store.setThreshold(23)
    await store.selectSpot(a.id)
    expect(store.threshold).toBe(17)
    await store.selectSpot(b.id)
    expect(store.threshold).toBe(23)
    expect(store.configuredSpotIds).toEqual([a.id, b.id])
  })
  it('replaces only the active spot and retains its settings', async () => {
    const store = setup()
    const [a, b, c] = store.spots
    await store.addConfiguredSpot(a.id)
    await store.addConfiguredSpot(b.id)
    store.setThreshold(23)
    expect(await store.replaceConfiguredSpot(a.id)).toBe(false)
    await store.replaceConfiguredSpot(c.id)
    expect(store.configuredSpotIds).toEqual([a.id, c.id])
    expect(store.selectedSpotId).toBe(c.id)
    expect(store.threshold).toBe(23)
  })
  it('rejects duplicates and an eleventh spot; selects a neighbor on removal', async () => {
    const store = setup()
    for (const spot of store.spots.slice(0, 10)) await store.addConfiguredSpot(spot.id)
    expect(await store.addConfiguredSpot(store.spots[10].id)).toBe(false)
    expect(await store.addConfiguredSpot(store.spots[0].id)).toBe(false)
    const ids = [...store.configuredSpotIds]
    await store.removeConfiguredSpot(ids[9])
    expect(store.selectedSpotId).toBe(ids[8])
    for (const id of ids.slice(0, 9)) await store.removeConfiguredSpot(id)
    expect(store.configuredSpotIds).toEqual([])
  })
  it('preserves the list across device switches and rejects adding on E1002', async () => {
    const store = setup()
    await store.addConfiguredSpot(store.spots[0].id)
    store.setSelectedBoardId(BOARD_IDS.E1002)
    expect(await store.addConfiguredSpot(store.spots[1].id)).toBe(false)
    store.setSelectedBoardId(BOARD_IDS.E1003)
    expect(store.configuredSpotIds).toHaveLength(1)
  })
  it('round-trips a ten-spot shared URL including independent settings', async () => {
    const store = setup()
    for (const [index, spot] of store.spots.slice(0, 10).entries()) {
      await store.addConfiguredSpot(spot.id)
      store.setThreshold(index + 10)
    }
    const url = configurationUrl(store, 'http://localhost/?configure')
    expect(url.search.length).toBeLessThan(5000)
    setActivePinia(createPinia())
    const restored = setup()
    expect(applyConfigurationUrl(restored, url.search)).toBe(true)
    expect(restored.configuredSpotIds).toHaveLength(10)
    for (const [index, id] of restored.configuredSpotIds.entries()) {
      await restored.selectSpot(id)
      expect(restored.threshold).toBe(index + 10)
    }
  })
  it('rejects duplicate or nested spots in a shared link', async () => {
    const store = setup()
    await store.addConfiguredSpot('brouwersdam')
    const url = configurationUrl(store, 'http://localhost/?configure')
    const entries = JSON.parse(url.searchParams.get('spots'))
    url.searchParams.set('spots', JSON.stringify([entries[0], entries[0]]))
    expect(applyConfigurationUrl(store, url.search)).toBe(false)
    url.searchParams.set('spots', JSON.stringify([url.search]))
    expect(applyConfigurationUrl(store, url.search)).toBe(false)
  })
  it('continues to accept older full-query spot links', async () => {
    const store = setup()
    await store.addConfiguredSpot('brouwersdam')
    const url = configurationUrl(store, 'http://localhost/?configure')
    const oldEntry = configurationUrl(store, 'http://localhost/?configure', { includeSpots: false }).search
    url.searchParams.set('spots', JSON.stringify([oldEntry]))
    expect(applyConfigurationUrl(store, url.search)).toBe(true)
    expect(store.configuredSpotIds).toEqual(['brouwersdam'])
  })
  it('migrates an existing single spot without replacing its device choice', () => {
    const store = setup()
    persistConfigurator({ store }, { getItem: () => JSON.stringify({ selectedSpotId: 'edam', selectedBoardId: BOARD_IDS.E1002, threshold: 21 }) }, { subscribe: false })
    expect(store.selectedBoardId).toBe(BOARD_IDS.E1002)
    expect(store.configuredSpotIds).toEqual(['edam'])
    expect(store.threshold).toBe(21)
  })

  it('restores the list and per-spot settings after reload', async () => {
    let saved
    const storage = { getItem: () => saved, setItem: (_, value) => { saved = value } }
    const store = setup()
    persistConfigurator({ store }, storage)
    const [a, b] = store.spots
    await store.addConfiguredSpot(a.id)
    store.setThreshold(17)
    await store.addConfiguredSpot(b.id)
    store.setThreshold(23)
    setActivePinia(createPinia())
    const restored = setup()
    persistConfigurator({ store: restored }, storage)
    expect(restored.configuredSpotIds).toEqual([a.id, b.id])
    expect(restored.threshold).toBe(23)
    await restored.selectSpot(a.id)
    expect(restored.threshold).toBe(17)
  })
})
