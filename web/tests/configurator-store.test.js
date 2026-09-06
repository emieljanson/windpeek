import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import { displayConfigurationFromStore } from '../src/config/configuration'
import { useConfiguratorStore } from '../src/stores/configurator'
import { DEFAULT_FORECAST_MODEL_ID, getForecastModel } from '../src/forecast/models'
import { getSpot } from '../src/spots'

function memoryStorage() {
  const values = new Map()
  return {
    getItem: (key) => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
    removeItem: (key) => values.delete(key),
  }
}

function deferred() {
  let resolve
  let reject
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise
    reject = rejectPromise
  })
  return { promise, resolve, reject }
}

function liveForecast(
  spotId = 'brouwersdam',
  retrievedAt = 1_777_000_000_000,
  modelId = DEFAULT_FORECAST_MODEL_ID,
) {
  const spot = getSpot(spotId)
  const model = getForecastModel(modelId)
  return {
    schemaVersion: 2,
    spotId,
    spotName: spot.displayName,
    timezone: spot.timezone,
    provider: 'OPEN-METEO',
    modelId,
    model: model.screenLabel,
    updatedTime: '26 AUG 2PM',
    retrievedAt,
    days: Array.from({ length: 5 }, (_, day) => ({
      localDate: `2026-08-${26 + day}`,
      day: day === 0 ? 'TODAY' : 'THURSDAY',
      date: `${26 + day} AUG`,
      samples: [8, 11, 14, 17, 20].map((hour) => ({
        time: String(hour).padStart(2, '0'), sustainedKt: 12, gustKt: 18,
        destinationDegrees: 270, available: true, weather: 1,
        temperatureTenthsC: 125, temperatureAvailable: true,
      })),
    })),
  }
}

function forecastSet(...forecasts) {
  return Object.fromEntries(forecasts.map((forecast) => [forecast.modelId, forecast]))
}

function tideFor(spotId = 'brouwersdam', capability = 'available') {
  const spot = getSpot(spotId)
  return {
    schemaVersion: 2,
    spotId,
    timezone: spot.timezone,
    provider: 'OPEN-METEO MARINE',
    retrievedAt: 1_777_000_000_000,
    capability,
    samples: capability === 'available'
      ? Array.from({ length: 120 }, (_, index) => ({
        timestamp: 1_777_000_000 + index * 3600,
        localDate: `2026-08-${String(26 + Math.floor(index / 24)).padStart(2, '0')}`,
        localTime: `${String(index % 24).padStart(2, '0')}:00`,
        seaLevelMm: Math.round(Math.sin(index / 6) * 800),
      }))
      : [],
    extrema: capability === 'available'
      ? Array.from({ length: 20 }, (_, index) => {
        const sampleIndex = 3 + index * 6
        return {
          timestamp: 1_777_000_000 + sampleIndex * 3600,
          localDate: `2026-08-${String(26 + Math.floor(sampleIndex / 24)).padStart(2, '0')}`,
          localTime: `${String(sampleIndex % 24).padStart(2, '0')}:00`,
          seaLevelMm: Math.round(Math.sin(sampleIndex / 6) * 800),
          type: index % 2 === 0 ? 'high' : 'low',
        }
      })
      : [],
  }
}

describe('configurator store', () => {
  beforeEach(() => setActivePinia(createPinia()))
  afterEach(() => vi.restoreAllMocks())

  it('starts with the current display defaults', () => {
    const store = useConfiguratorStore()
    expect(store.showThreshold).toBe(false)
    expect(store.threshold).toBe(17)
    expect(store.showWeather).toBe(true)
    expect(store.showTemperature).toBe(false)
    expect(store.showTide).toBe(false)
    expect(store.showDedicatedFooter).toBe(false)
    expect(['12-hour', '24-hour']).toContain(store.timeFormat)
    expect(store.temperatureUnit).toBe('celsius')
    expect(store.selectedSpotId).toBe('brouwersdam')
    expect(store.selectedModelId).toBe('best_match')
    expect(store.forecastSource).toBe('demo')
    expect(store.forecastLabel).toBe('Demo')
    expect(store.hasUserSpotIntent).toBe(false)
    expect(store.nearbyDefaultStatus).toBe('idle')
  })

  it('applies one nearby default through the existing spot refresh path', async () => {
    const store = useConfiguratorStore()
    const forecastFetcher = vi.fn(async (spot) => forecastSet(liveForecast(spot.id)))
    const tideFetcher = vi.fn(async (spot) => tideFor(spot.id))
    await store.initializeTide({ fetcher: tideFetcher, storage: memoryStorage() })

    await expect(store.initializeNearbyDefault({
      locationFetcher: vi.fn().mockResolvedValue({ latitude: 52.5126, longitude: 5.0486 }),
      fetcher: forecastFetcher,
      tideFetcher,
      storage: memoryStorage(),
    })).resolves.toBe(true)

    expect(store.selectedSpotId).toBe('edam')
    expect(store.nearbyDefaultStatus).toBe('applied')
    expect(forecastFetcher).toHaveBeenCalledWith(expect.objectContaining({ id: 'edam' }), {})
    expect(tideFetcher).toHaveBeenLastCalledWith(expect.objectContaining({ id: 'edam' }), {})
  })

  it('ignores the initial Brouwersdam tide after the nearby spot loads', async () => {
    const store = useConfiguratorStore()
    const initialTide = deferred()
    const tideFetcher = vi.fn()
      .mockImplementationOnce(() => initialTide.promise)
      .mockResolvedValueOnce(tideFor('edam'))
    const initialTideRequest = store.initializeTide({ fetcher: tideFetcher })

    await store.initializeNearbyDefault({
      locationFetcher: vi.fn().mockResolvedValue({ latitude: 52.5126, longitude: 5.0486 }),
      fetcher: vi.fn().mockResolvedValue(forecastSet(liveForecast('edam'))),
      tideFetcher,
    })
    await vi.waitFor(() => expect(store.tide?.spotId).toBe('edam'))

    initialTide.resolve(tideFor('brouwersdam'))
    await expect(initialTideRequest).resolves.toBe(false)
    expect(store.tide.spotId).toBe('edam')
  })

  it('performs only one nearby lookup when initializers overlap', async () => {
    const store = useConfiguratorStore()
    const location = deferred()
    const locationFetcher = vi.fn(() => location.promise)

    const first = store.initializeNearbyDefault({
      locationFetcher,
      fetcher: vi.fn().mockResolvedValue(forecastSet(liveForecast('edam'))),
    })
    const second = store.initializeNearbyDefault({ locationFetcher })

    expect(locationFetcher).toHaveBeenCalledOnce()
    location.resolve(null)
    await expect(first).resolves.toBe(false)
    await expect(second).resolves.toBe(false)
    expect(locationFetcher).toHaveBeenCalledOnce()
    expect(store.selectedSpotId).toBe('brouwersdam')
    expect(store.nearbyDefaultStatus).toBe('ignored')
  })

  it('does not let a late nearby result override user spot intent', async () => {
    const store = useConfiguratorStore()
    const location = deferred()
    const nearby = store.initializeNearbyDefault({ locationFetcher: vi.fn(() => location.promise) })

    store.markUserSpotIntent()
    store.selectedSpotId = 'castricum-aan-zee'
    location.resolve({ latitude: 52.5126, longitude: 5.0486 })

    await expect(nearby).resolves.toBe(false)
    expect(store.hasUserSpotIntent).toBe(true)
    expect(store.selectedSpotId).toBe('castricum-aan-zee')
    expect(store.nearbyDefaultStatus).toBe('ignored')
  })

  it('does not apply a late nearby result after the default spot changed', async () => {
    const store = useConfiguratorStore()
    const location = deferred()
    const nearby = store.initializeNearbyDefault({ locationFetcher: vi.fn(() => location.promise) })

    store.selectedSpotId = 'castricum-aan-zee'
    location.resolve({ latitude: 52.5126, longitude: 5.0486 })

    await expect(nearby).resolves.toBe(false)
    expect(store.hasUserSpotIntent).toBe(false)
    expect(store.selectedSpotId).toBe('castricum-aan-zee')
  })

  it('keeps Brouwersdam when the nearby lookup has no usable result', async () => {
    const store = useConfiguratorStore()

    await expect(store.initializeNearbyDefault({
      locationFetcher: vi.fn().mockResolvedValue(null),
    })).resolves.toBe(false)

    expect(store.selectedSpotId).toBe('brouwersdam')
    expect(store.nearbyDefaultStatus).toBe('ignored')
  })

  it('offers a partly different regional model list for each location', () => {
    const store = useConfiguratorStore()
    store.forecastsByModel = {}
    expect(store.availableForecastModels.map((model) => model.id)).toEqual([
      'best_match', 'ecmwf_ifs', 'icon_seamless', 'ncep_gfs_seamless',
      'knmi_harmonie', 'dmi_harmonie',
    ])

    store.personalSpots.push({
      id: 'personal-tokyo',
      name: 'Tokyo',
      displayName: 'TOKYO',
      latitude: 35.6764,
      longitude: 139.65,
      timezone: 'Asia/Tokyo',
      countryCode: 'jp',
      personal: true,
    })
    store.selectedSpotId = 'personal-tokyo'
    expect(store.availableForecastModels.map((model) => model.id)).toEqual([
      'best_match', 'ecmwf_ifs', 'icon_seamless', 'ncep_gfs_seamless', 'jma_msm',
    ])
  })

  it('adds a confirmed personal spot to the selectable catalog and local storage', () => {
    const storage = memoryStorage()
    const store = useConfiguratorStore()
    const spot = store.addPersonalSpot({
      name: 'Edam harbour',
      latitude: 52.50673,
      longitude: 5.07729,
      timezone: 'Europe/Amsterdam',
      providerRef: 'geoapify:edam-id',
    }, { storage })

    expect(spot).toMatchObject({ name: 'Edam harbour', personal: true })
    expect(store.spots.at(-1)).toEqual(spot)
    expect(store.spotById(spot.id)).toEqual(spot)

    store.$dispose()
    setActivePinia(createPinia())
    const restored = useConfiguratorStore()
    restored.loadPersonalSpots({ storage })
    expect(restored.spotById(spot.id)).toEqual(spot)
  })

  it.each([
    [true, '12-hour'],
    [false, '24-hour'],
  ])('writes the browser hour12=%s convention into active configuration', (hour12, expected) => {
    vi.spyOn(Intl, 'DateTimeFormat').mockImplementation(function MockDateTimeFormat() {
      return { resolvedOptions: () => ({ hour12 }) }
    })
    setActivePinia(createPinia())

    const store = useConfiguratorStore()

    expect(store.timeFormat).toBe(expected)
    expect(displayConfigurationFromStore(store).timeFormat).toBe(expected)
  })

  it('keeps row preferences separate and only enables tide after a supported result', async () => {
    const store = useConfiguratorStore()
    expect(store.setShowWeather(false)).toBe(true)
    expect(store.setShowTemperature(true)).toBe(true)
    expect(store.setShowTide(true)).toBe(false)

    await expect(store.initializeTide({
      fetcher: vi.fn().mockResolvedValue(tideFor()),
      storage: memoryStorage(),
    })).resolves.toBe(true)

    expect(store.tideStatus).toBe('available')
    expect(store.tideAvailable).toBe(true)
    expect(store.setShowTide(true)).toBe(true)
    expect(store.effectiveShowTide).toBe(true)
  })

  it('distinguishes unsupported tide from a failed capability check', async () => {
    const unsupported = useConfiguratorStore()
    await unsupported.initializeTide({
      fetcher: vi.fn().mockResolvedValue(tideFor('brouwersdam', 'unsupported')),
      storage: memoryStorage(),
    })
    expect(unsupported.tideStatus).toBe('unsupported')
    expect(unsupported.tideMessage).toContain('not available')

    unsupported.$dispose()
    setActivePinia(createPinia())
    const failed = useConfiguratorStore()
    await failed.initializeTide({
      fetcher: vi.fn().mockRejectedValue(new Error('offline')),
      storage: memoryStorage(),
    })
    expect(failed.tideStatus).toBe('failed')
    expect(failed.tideMessage).toContain('Could not check')
  })

  it('keeps cached tide timing when a refresh fails', async () => {
    const storage = memoryStorage()
    const seed = useConfiguratorStore()
    await seed.initializeTide({ fetcher: vi.fn().mockResolvedValue(tideFor()), storage })
    seed.$dispose()
    setActivePinia(createPinia())
    const store = useConfiguratorStore()

    await store.initializeTide({ fetcher: vi.fn().mockRejectedValue(new Error('offline')), storage })

    expect(store.tideStatus).toBe('cached')
    expect(store.tideAvailable).toBe(true)
    expect(store.tideMessage).toContain('cached')
  })

  it('ignores a late marine response from the previous spot', async () => {
    const store = useConfiguratorStore()
    const first = deferred()
    const loading = store.initializeTide({ fetcher: vi.fn(() => first.promise) })
    store.selectedSpotId = 'edam'
    await store.refreshTide({ fetcher: vi.fn().mockResolvedValue(tideFor('edam')) })

    first.resolve(tideFor('brouwersdam'))
    await expect(loading).resolves.toBe(false)

    expect(store.tide.spotId).toBe('edam')
    expect(store.tideStatus).toBe('available')
  })

  it('keeps threshold visibility separate from its last valid value', () => {
    const store = useConfiguratorStore()
    expect(store.setShowThreshold(true)).toBe(true)
    expect(store.setThreshold(5)).toBe(true)
    expect(store.setThreshold(35)).toBe(true)
    expect(store.threshold).toBe(35)
    expect(store.setShowThreshold(false)).toBe(true)
    expect(store.threshold).toBe(35)
    expect(store.setShowThreshold(true)).toBe(true)
    expect(store.threshold).toBe(35)
  })

  it('maps the combined temperature choice without discarding the hidden unit', () => {
    const store = useConfiguratorStore()

    expect(store.setTemperatureChoice('fahrenheit')).toBe(true)
    expect(store.temperatureChoice).toBe('fahrenheit')
    expect(store.showTemperature).toBe(true)
    expect(store.temperatureUnit).toBe('fahrenheit')
    expect(store.setTemperatureChoice('hide')).toBe(true)
    expect(store.temperatureChoice).toBe('hide')
    expect(store.showTemperature).toBe(false)
    expect(store.temperatureUnit).toBe('fahrenheit')
    expect(store.setTemperatureChoice('celsius')).toBe(true)
    expect(store.showTemperature).toBe(true)
    expect(store.temperatureUnit).toBe('celsius')
    expect(store.setTemperatureChoice('kelvin')).toBe(false)
  })

  it('preserves the last valid configuration after invalid input', () => {
    const store = useConfiguratorStore()
    store.setShowThreshold(true)
    store.setThreshold(17)
    expect(store.setShowThreshold('yes')).toBe(false)
    expect(store.setThreshold(36)).toBe(false)
    expect(store.setThreshold('unknown')).toBe(false)
    expect(store.showThreshold).toBe(true)
    expect(store.threshold).toBe(17)
  })

  it('announces a rejected display render without replacing the current preview', () => {
    const store = useConfiguratorStore()
    const published = store.publishedForecast

    store.reportConfigurationRenderFailure()

    expect(store.publishedForecast).toBe(published)
    expect(store.forecastStatus).toBe('warning')
    expect(store.forecastLabel).toBe('Preview unchanged')
    expect(store.forecastMessage).toContain('last valid preview')
  })

  it('keeps the demo label until the current forecast bitmap is published', async () => {
    const store = useConfiguratorStore()
    const forecast = liveForecast()
    await expect(store.initializeForecast({
      fetcher: vi.fn().mockResolvedValue(forecastSet(forecast)),
      storage: memoryStorage(),
    })).resolves.toBe(true)
    expect(store.forecast).toEqual(forecast)
    expect(store.forecastStatus).toBe('rendering')
    expect(store.forecastLabel).toBe('Demo')
    expect(store.pendingForecastRevision).toBe(store.forecastRevision)

    expect(store.publishForecast(store.forecastRevision)).toBe(true)
    expect(store.forecastStatus).toBe('ready')
    expect(store.forecastLabel).toBe('')
  })

  it('shows an explicit demo fallback when the first request fails', async () => {
    const store = useConfiguratorStore()
    await expect(store.initializeForecast({
      fetcher: vi.fn().mockRejectedValue(new Error('offline')),
      storage: memoryStorage(),
    })).resolves.toBe(false)
    expect(store.forecastSource).toBe('demo')
    expect(store.forecastStatus).toBe('warning')
    expect(store.forecastLabel).toBe('Demo')
    expect(store.forecastMessage).toContain('demo')
  })

  it('keeps the last successful forecast when a later refresh fails', async () => {
    const store = useConfiguratorStore()
    const first = liveForecast()
    const fetcher = vi.fn()
      .mockResolvedValueOnce(forecastSet(first))
      .mockRejectedValueOnce(new Error('offline'))
    await store.initializeForecast({ fetcher, storage: memoryStorage() })
    store.publishForecast(store.forecastRevision)
    await expect(store.refreshForecast({ fetcher, storage: memoryStorage() })).resolves.toBe(false)
    expect(store.forecast).toEqual(first)
    expect(store.forecastStatus).toBe('warning')
    expect(store.forecastLabel).toBe('Update delayed')
    expect(store.forecastMessage).toContain('last forecast')
  })

  it('uses cached data without mislabeling it as demo data', async () => {
    const storage = memoryStorage()
    const firstStore = useConfiguratorStore()
    const current = liveForecast()
    await firstStore.initializeForecast({ fetcher: vi.fn().mockResolvedValue(forecastSet(current)), storage })
    firstStore.$dispose()

    setActivePinia(createPinia())
    const store = useConfiguratorStore()
    await store.initializeForecast({ fetcher: vi.fn().mockRejectedValue(new Error('offline')), storage })
    expect(store.forecast).toEqual(current)
    expect(store.forecastSource).toBe('cache')
    expect(store.forecastLabel).toBe('Cached')
  })

  it('labels the old spot honestly if a newly selected spot cannot load', async () => {
    const store = useConfiguratorStore()
    const fetcher = vi.fn()
      .mockResolvedValueOnce(forecastSet(liveForecast('brouwersdam')))
      .mockRejectedValueOnce(new Error('offline'))
    await store.initializeForecast({ fetcher, storage: memoryStorage() })
    store.publishForecast(store.forecastRevision)

    await store.selectSpot('edam', { fetcher, storage: memoryStorage() })
    expect(store.selectedSpotId).toBe('edam')
    expect(store.forecast.spotId).toBe('brouwersdam')
    expect(store.forecastSource).toBe('previous')
    expect(store.forecastLabel).toBe('Previous spot')
    expect(store.forecastMessage).toContain('Still showing BROUWERSDAM')
  })

  it('loads another supported spot but display-only settings never fetch', async () => {
    const store = useConfiguratorStore()
    const fetcher = vi.fn(async (spot) => forecastSet(liveForecast(spot.id)))
    const storage = memoryStorage()
    await store.initializeForecast({ fetcher, storage })
    store.publishForecast(store.forecastRevision)
    expect(await store.selectSpot('edam', { fetcher, storage })).toBe(true)
    expect(fetcher).toHaveBeenCalledTimes(2)
    expect(fetcher.mock.calls[1][0]).toMatchObject({ id: 'edam', latitude: 52.5126, longitude: 5.0486 })
    expect(store.forecast.spotId).toBe('edam')

    store.setShowThreshold(true)
    store.setThreshold(24)
    expect(fetcher).toHaveBeenCalledTimes(2)
    await expect(store.selectSpot('nowhere', { fetcher, storage })).resolves.toBe(false)
    expect(fetcher).toHaveBeenCalledTimes(2)
  })

  it('switches between already loaded models without another network request', async () => {
    const store = useConfiguratorStore()
    const fetcher = vi.fn().mockResolvedValue(forecastSet(
      liveForecast(),
      liveForecast('brouwersdam', 1_777_000_000_000, 'ncep_gfs_seamless'),
    ))
    await store.initializeForecast({ fetcher, storage: memoryStorage() })
    store.publishForecast(store.forecastRevision)

    expect(await store.selectModel('ncep_gfs_seamless', { fetcher, storage: memoryStorage() })).toBe(true)
    expect(fetcher).toHaveBeenCalledOnce()
    expect(store.selectedModelId).toBe('ncep_gfs_seamless')
    expect(store.forecast).toMatchObject({ modelId: 'ncep_gfs_seamless', model: 'NOAA GFS' })
    expect(store.forecastStatus).toBe('rendering')
    expect(store.publishForecast(store.forecastRevision)).toBe(true)
    expect(store.forecastMessage).toBe('Live NOAA GFS forecast for Brouwersdam.')
  })

  it('changes the requested model during initial loading without duplicating the API call', async () => {
    const store = useConfiguratorStore()
    const request = deferred()
    const fetcher = vi.fn(() => request.promise)
    const loading = store.initializeForecast({ fetcher, storage: memoryStorage() })

    const selecting = store.selectModel('ncep_gfs_seamless', {
      fetcher,
      storage: memoryStorage(),
    })
    await Promise.resolve()
    expect(fetcher).toHaveBeenCalledOnce()

    request.resolve(forecastSet(
      liveForecast(),
      liveForecast('brouwersdam', 1_777_000_000_000, 'ncep_gfs_seamless'),
    ))
    await expect(selecting).resolves.toBe(true)
    await expect(loading).resolves.toBe(true)
    expect(store.forecast.modelId).toBe('ncep_gfs_seamless')
  })

  it('keeps a cached model labelled as cached while the live refresh is pending or fails', async () => {
    const storage = memoryStorage()
    const cachedBestFit = liveForecast()
    const cachedGfs = liveForecast('brouwersdam', 1_777_000_000_000, 'ncep_gfs_seamless')
    const seedStore = useConfiguratorStore()
    await seedStore.initializeForecast({
      fetcher: vi.fn().mockResolvedValue(forecastSet(cachedBestFit, cachedGfs)),
      storage,
    })
    seedStore.$dispose()

    setActivePinia(createPinia())
    const store = useConfiguratorStore()
    const request = deferred()
    const fetcher = vi.fn(() => request.promise)
    const loading = store.initializeForecast({ fetcher, storage })

    await store.selectModel('ncep_gfs_seamless', { fetcher, storage })
    expect(store.forecast.modelId).toBe('ncep_gfs_seamless')
    expect(store.publishForecast(store.forecastRevision)).toBe(true)
    expect(store.forecastSource).toBe('cache')
    expect(store.forecastLabel).toBe('Cached')
    expect(store.forecastMessage).toContain('Refreshing current data')
    expect(fetcher).toHaveBeenCalledOnce()

    request.reject(new Error('offline'))
    await expect(loading).resolves.toBe(false)
    expect(store.forecastSource).toBe('cache')
    expect(store.forecastLabel).toBe('Cached')
    expect(store.forecastMessage).toBe('Could not refresh. Showing the cached forecast.')
  })

  it('rejects unknown models without changing the current preview', async () => {
    const store = useConfiguratorStore()
    const current = store.forecast
    await expect(store.selectModel('magic-wind')).resolves.toBe(false)
    expect(store.selectedModelId).toBe('best_match')
    expect(store.forecast).toBe(current)
  })

  it('labels the previous model honestly when a requested model cannot load', async () => {
    const store = useConfiguratorStore()
    const fetcher = vi.fn()
      .mockResolvedValueOnce(forecastSet(liveForecast()))
      .mockRejectedValueOnce(new Error('offline'))
    await store.initializeForecast({ fetcher, storage: memoryStorage() })
    store.publishForecast(store.forecastRevision)

    await expect(store.selectModel('ncep_gfs_seamless', {
      fetcher,
      storage: memoryStorage(),
    })).resolves.toBe(false)

    expect(store.selectedModelId).toBe('ncep_gfs_seamless')
    expect(store.forecast.modelId).toBe('best_match')
    expect(store.forecastLabel).toBe('Previous model')
    expect(store.forecastMessage).toBe('Could not load NOAA GFS. Still showing BEST MATCH.')
  })

  it('does not publish an old pending forecast under a newly selected spot', async () => {
    const store = useConfiguratorStore()
    await store.initializeForecast({
      fetcher: vi.fn().mockResolvedValue(forecastSet(liveForecast())),
      storage: memoryStorage(),
    })
    const oldRevision = store.forecastRevision
    const edam = deferred()

    const selectingEdam = store.selectSpot('edam', { fetcher: () => edam.promise, storage: memoryStorage() })

    expect(store.publishForecast(oldRevision)).toBe(false)
    edam.resolve(forecastSet(liveForecast('edam')))
    await selectingEdam
    expect(store.publishForecast(store.forecastRevision)).toBe(true)
    expect(store.forecastMessage).toBe('Live Best Match forecast for Edam.')
  })

  it('ignores an older spot request that resolves after the newest one', async () => {
    const store = useConfiguratorStore()
    await store.initializeForecast({
      fetcher: vi.fn().mockResolvedValue(forecastSet(liveForecast())),
      storage: memoryStorage(),
    })
    store.publishForecast(store.forecastRevision)
    const edam = deferred()
    const castricum = deferred()

    const firstSelection = store.selectSpot('edam', { fetcher: () => edam.promise, storage: memoryStorage() })
    const secondSelection = store.selectSpot('castricum-aan-zee', {
      fetcher: () => castricum.promise,
      storage: memoryStorage(),
    })
    castricum.resolve(forecastSet(liveForecast('castricum-aan-zee')))
    await secondSelection
    store.publishForecast(store.forecastRevision)
    edam.resolve(forecastSet(liveForecast('edam')))

    await expect(firstSelection).resolves.toBe(false)
    expect(store.forecast.spotId).toBe('castricum-aan-zee')
    expect(store.forecastMessage).toBe('Live Best Match forecast for Castricum aan Zee.')
  })

  it('restores the last published forecast when a new bitmap is rejected', async () => {
    const store = useConfiguratorStore()
    const fetcher = vi.fn()
      .mockResolvedValueOnce(forecastSet(liveForecast()))
      .mockResolvedValueOnce(forecastSet(liveForecast('edam')))
    await store.initializeForecast({ fetcher, storage: memoryStorage() })
    store.publishForecast(store.forecastRevision)
    await store.selectSpot('edam', { fetcher, storage: memoryStorage() })

    expect(store.rejectForecastPublication(store.forecastRevision)).toBe(true)
    expect(store.forecast.spotId).toBe('brouwersdam')
    expect(store.pendingForecastRevision).toBeNull()
    expect(store.forecastStatus).toBe('warning')
    expect(store.forecastLabel).toBe('Previous spot')
  })

  it('restores the published model when a selected model bitmap is rejected', async () => {
    const store = useConfiguratorStore()
    const bestFit = liveForecast()
    const gfs = liveForecast('brouwersdam', 1_777_000_000_000, 'ncep_gfs_seamless')
    await store.initializeForecast({
      fetcher: vi.fn().mockResolvedValue(forecastSet(bestFit, gfs)),
      storage: memoryStorage(),
    })
    store.publishForecast(store.forecastRevision)
    await store.selectModel('ncep_gfs_seamless', { storage: memoryStorage() })

    expect(store.rejectForecastPublication(store.forecastRevision)).toBe(true)
    expect(store.selectedModelId).toBe('ncep_gfs_seamless')
    expect(store.forecast.modelId).toBe('best_match')
    expect(store.forecastLabel).toBe('Previous model')
    expect(store.forecastMessage).toBe('Could not show NOAA GFS. Still showing BEST MATCH.')
  })
})
