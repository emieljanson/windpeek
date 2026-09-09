import { MODULE_IDS, validModuleOrder } from '../config/modules'
import { defineStore } from 'pinia'
import { brouwersdamForecast } from '../fixtures/brouwersdam'
import { readCachedForecast, writeCachedForecasts } from '../forecast/forecastCache'
import {
  ALWAYS_FORECAST_MODEL_IDS,
  DEFAULT_FORECAST_MODEL_ID,
  forecastModelsForSpot,
  getForecastModel,
} from '../forecast/models'
import { fetchOpenMeteoForecasts } from '../forecast/openMeteo'
import { fetchOpenMeteoSwell, SWELL_MODELS } from '../forecast/openMeteoSwell'
import { fetchOpenMeteoTide } from '../forecast/openMeteoMarine'
import { fetchIpLocation } from '../location/ipLocation'
import { readCachedTide, writeCachedTide } from '../forecast/tideCache'
import {
  createDefaultDisplayConfiguration,
  BOARD_ID,
  SUPPORTED_BOARD_IDS,
  TEMPERATURE_CHOICES,
  TEMPERATURE_UNITS,
  TIME_FORMATS,
} from '../config/configuration'
import {
  MAX_THRESHOLD,
  MIN_THRESHOLD,
} from '../renderer/contract'
import { DEFAULT_SPOT_ID, SPOTS } from '../spots'
import { findNearbyDefaultSpot } from '../spots/nearestSpot'
import {
  createPersonalSpot,
  readPersonalSpots,
  writePersonalSpot,
} from '../spots/personalSpots'

export { DEFAULT_THRESHOLD, MAX_THRESHOLD, MIN_THRESHOLD } from '../renderer/contract'

export const useConfiguratorStore = defineStore('configurator', {
  state: () => {
    const displayConfiguration = createDefaultDisplayConfiguration()
    return {
      showThreshold: displayConfiguration.showThreshold,
      threshold: displayConfiguration.threshold,
      showWeather: displayConfiguration.showWeather,
      showTemperature: displayConfiguration.showTemperature,
      showTide: displayConfiguration.showTide,
      showDedicatedFooter: displayConfiguration.showDedicatedFooter,
      timeFormat: displayConfiguration.timeFormat,
      temperatureUnit: displayConfiguration.temperatureUnit,
      temperatureUnitInitialized: false,
      selectedBoardId: BOARD_ID,
      selectedSpotId: DEFAULT_SPOT_ID,
      hasUserSpotIntent: false,
      nearbyDefaultStatus: 'idle',
      personalSpots: readPersonalSpots(),
      selectedModelId: DEFAULT_FORECAST_MODEL_ID,
      forecastsByModel: { [DEFAULT_FORECAST_MODEL_ID]: brouwersdamForecast },
      forecast: brouwersdamForecast,
      publishedForecast: brouwersdamForecast,
      forecastRevision: 0,
      pendingForecastRevision: null,
      pendingForecastSpotId: null,
      pendingForecastModelId: null,
      pendingForecastSource: null,
      forecastStatus: 'idle',
      forecastSource: 'demo',
      forecastMessage: 'Demo forecast. Loading current Brouwersdam weather…',
      forecastLabel: 'Demo',
      forecastInitialized: false,
      forecastRequestId: 0,
      forecastRequestInFlight: false,
      swellFocus: false,
      windSize: 'large',
      swellSize: 'off',
      selectedSwellModelId: 'best_match',
      moduleOrder: [...MODULE_IDS],
      swell: null,
      swellStatus: 'idle',
      swellRequestId: 0,
      tide: null,
      tideStatus: 'idle',
      tideMessage: 'Tide availability has not been checked yet.',
      tideInitialized: false,
      tideRequestId: 0,
    }
  },
  getters: {
    spots: (state) => [...SPOTS, ...state.personalSpots],
    spotById() {
      return (spotId) => this.spots.find((spot) => spot.id === spotId) ?? null
    },
    weatherSize: (state) => state.showTemperature ? 'large' : state.showWeather ? 'small' : 'off',
    temperatureChoice: (state) => state.showTemperature ? state.temperatureUnit : 'hide',
    tideAvailable: (state) =>
      ['available', 'cached'].includes(state.tideStatus) &&
      state.tide?.capability === 'available',
    effectiveShowTide() {
      return this.showTide && this.tideAvailable
    },
    availableForecastModels() {
      const spot = this.spotById(this.selectedSpotId)
      if (!spot) return []
      const candidates = forecastModelsForSpot(spot)
      const hasCurrentResults = Object.values(this.forecastsByModel)
        .some((forecast) => forecast?.spotId === spot.id)
      if (!hasCurrentResults) return candidates
      const availableIds = new Set([
        ...ALWAYS_FORECAST_MODEL_IDS,
        ...Object.keys(this.forecastsByModel),
      ])
      return candidates.filter((model) => availableIds.has(model.id))
    },
  },
  actions: {
    markUserSpotIntent() {
      this.hasUserSpotIntent = true
    },
    async initializeNearbyDefault({
      locationFetcher = fetchIpLocation,
      ...selectionOptions
    } = {}) {
      if (this.nearbyDefaultStatus !== 'idle') return false
      this.nearbyDefaultStatus = 'resolving'

      if ((this.hasUserSpotIntent || this.selectedSpotId !== DEFAULT_SPOT_ID) && this.temperatureUnitInitialized) {
        this.nearbyDefaultStatus = 'ignored'
        return false
      }

      let coordinates
      try {
        coordinates = await locationFetcher()
      } catch {
        this.nearbyDefaultStatus = 'ignored'
        return false
      }

      if (!this.temperatureUnitInitialized && /^[a-z]{2}$/i.test(coordinates?.countryCode ?? '')) {
        this.$patch({
          temperatureUnit: ['US', 'BS', 'BZ', 'KY', 'PW', 'FM', 'MH', 'LR'].includes(coordinates.countryCode.toUpperCase()) ? 'fahrenheit' : 'celsius',
          temperatureUnitInitialized: true,
        })
      }

      if (this.hasUserSpotIntent || this.selectedSpotId !== DEFAULT_SPOT_ID) {
        this.nearbyDefaultStatus = 'ignored'
        return false
      }

      const spot = findNearbyDefaultSpot(coordinates)
      if (!spot) {
        this.nearbyDefaultStatus = 'ignored'
        return false
      }
      if (spot.id !== DEFAULT_SPOT_ID) await this.selectSpot(spot.id, selectionOptions)
      this.nearbyDefaultStatus = 'applied'
      return true
    },
    setSelectedBoardId(value) {
      if (!SUPPORTED_BOARD_IDS.includes(value)) return false
      this.selectedBoardId = value
      return true
    },
    loadPersonalSpots({ storage } = {}) {
      this.personalSpots = readPersonalSpots(storage)
      return this.personalSpots
    },
    addPersonalSpot(input, { storage } = {}) {
      const spot = createPersonalSpot(input)
      if (!writePersonalSpot(spot, storage)) return null
      const index = this.personalSpots.findIndex((candidate) => candidate.id === spot.id)
      if (index >= 0) this.personalSpots[index] = spot
      else this.personalSpots.push(spot)
      return spot
    },
    setShowThreshold(value) {
      if (typeof value !== 'boolean') return false
      this.showThreshold = value
      return true
    },
    setThreshold(value) {
      const threshold = Number(value)
      if (!Number.isFinite(threshold) || threshold < MIN_THRESHOLD || threshold > MAX_THRESHOLD) {
        return false
      }
      this.threshold = Math.round(threshold)
      return true
    },
    setWeatherSize(value) {
      if (!['off', 'small', 'large'].includes(value)) return false
      const order = this.moduleOrder.filter(id => id !== 'temperature')
      order.splice(order.indexOf('weather') + 1, 0, 'temperature')
      this.$patch({ showWeather: value !== 'off', showTemperature: value === 'large', moduleOrder: order })
      return true
    },
    setShowWeather(value) {
      if (typeof value !== 'boolean') return false
      this.showWeather = value
      return true
    },
    setShowTemperature(value) {
      if (typeof value !== 'boolean') return false
      this.showTemperature = value
      return true
    },
    setShowTide(value) {
      if (typeof value !== 'boolean' || (value && !this.tideAvailable)) return false
      this.showTide = value
      return true
    },
    setShowDedicatedFooter(value) {
      if (typeof value !== 'boolean') return false
      this.showDedicatedFooter = value
      return true
    },
    setTimeFormat(value) {
      if (!TIME_FORMATS.includes(value)) return false
      this.timeFormat = value
      return true
    },
    setTemperatureUnit(value) {
      if (!TEMPERATURE_UNITS.includes(value)) return false
      this.$patch({ temperatureUnit: value, temperatureUnitInitialized: true })
      return true
    },
    setTemperatureChoice(value) {
      if (!TEMPERATURE_CHOICES.includes(value)) return false
      if (value === 'hide') {
        this.showTemperature = false
      } else {
        this.setTemperatureUnit(value)
        this.showTemperature = true
      }
      return true
    },
    reportConfigurationRenderFailure() {
      this.forecastStatus = 'warning'
      this.forecastLabel = 'Preview unchanged'
      this.forecastMessage = 'Could not apply that display change. Showing the last valid preview.'
    },
    moveModule(id, targetIndex) {
      if (!this.moduleOrder.includes(id) || !Number.isInteger(targetIndex) || targetIndex < 0 || targetIndex >= MODULE_IDS.length) return false
      const order = this.moduleOrder.filter(module => module !== id)
      order.splice(targetIndex, 0, id)
      this.moduleOrder = order
      return true
    },
    setModuleOrder(order) {
      if (!validModuleOrder(order)) return false
      this.moduleOrder = [...order]
      return true
    },
    setModuleSize(module, size) {
      if (!['wind', 'swell'].includes(module) || !['off', 'small', 'large'].includes(size)) return false
      this[`${module}Size`] = size
      this.swellFocus = this.swellSize !== 'off'
      if (this.swellFocus && ['idle', 'failed'].includes(this.swellStatus)) void this.refreshSwell()
      return true
    },
    setSwellModel(value) {
      if (!SWELL_MODELS.some(model => model.value === value)) return false
      if (this.selectedSwellModelId === value) return true
      this.selectedSwellModelId = value
      this.swellRequestId += 1
      this.swell = null
      this.swellStatus = 'idle'
      if (this.swellSize !== 'off') void this.refreshSwell()
      return true
    },
    async refreshSwell({ fetcher = fetchOpenMeteoSwell, ...options } = {}) {
      const spot = this.spotById(this.selectedSpotId)
      if (!spot) return false
      const requestId = ++this.swellRequestId
      this.swellStatus = 'loading'
      try {
        const swell = await fetcher(spot, { ...options, model: this.selectedSwellModelId })
        if (requestId !== this.swellRequestId || spot.id !== this.selectedSpotId) return false
        this.swell = swell
        this.swellStatus = swell.available ? 'ready' : 'unavailable'
        return true
      } catch {
        if (requestId !== this.swellRequestId || spot.id !== this.selectedSpotId) return false
        this.swellStatus = 'failed'
        return false
      }
    },
    async initializeTide(options = {}) {
      if (this.tideInitialized) return false
      this.tideInitialized = true
      return this.refreshTide(options)
    },
    async refreshTide({
      fetcher = fetchOpenMeteoTide,
      storage,
      ...fetchOptions
    } = {}) {
      const spot = this.spotById(this.selectedSpotId)
      if (!spot) return false
      const requestId = ++this.tideRequestId
      const cached = readCachedTide(spot.id, spot.timezone, storage)
      if (cached) {
        this.tide = cached
        this.tideStatus = cached.capability === 'available' ? 'cached' : 'unsupported'
        this.tideMessage = cached.capability === 'available'
          ? 'Showing cached tide timing while current data loads.'
          : 'Tide is not available for this spot.'
      } else {
        this.tide = null
        this.tideStatus = 'loading'
        this.tideMessage = `Checking tide availability for ${spot.name}…`
      }
      try {
        const tide = await fetcher(spot, fetchOptions)
        if (requestId !== this.tideRequestId || this.selectedSpotId !== spot.id) return false
        writeCachedTide(tide, storage)
        this.tide = tide
        if (tide.capability === 'available') {
          this.tideStatus = 'available'
          this.tideMessage = 'Indicative tide timing from Open-Meteo. Not for navigation.'
        } else {
          this.tideStatus = 'unsupported'
          this.tideMessage = 'Tide is not available for this spot.'
        }
        return true
      } catch {
        if (requestId !== this.tideRequestId || this.selectedSpotId !== spot.id) return false
        if (cached?.capability === 'available') {
          this.tide = cached
          this.tideStatus = 'cached'
          this.tideMessage = 'Could not refresh tide timing. Showing cached data; not for navigation.'
        } else {
          this.tide = null
          this.tideStatus = 'failed'
          this.tideMessage = 'Could not check tide availability. Try again later.'
        }
        return false
      }
    },
    async initializeForecast(options = {}) {
      if (this.forecastInitialized) return false
      this.forecastInitialized = true
      return this.refreshForecast(options)
    },
    async refreshForecast({
      fetcher = fetchOpenMeteoForecasts,
      storage,
      ...fetchOptions
    } = {}) {
      const spot = this.spotById(this.selectedSpotId)
      if (!spot) return false
      const requestId = ++this.forecastRequestId
      this.forecastRequestInFlight = true
      this.pendingForecastRevision = null
      this.pendingForecastSpotId = null
      this.pendingForecastModelId = null
      this.pendingForecastSource = null
      this.forecastsByModel = {}
      const cached = readCachedForecast(spot.id, this.selectedModelId, storage)
      if (cached) {
        this.forecastsByModel = { [cached.modelId]: cached }
        this.forecast = cached
        this.publishedForecast = cached
        this.forecastSource = 'cache'
        this.forecastLabel = 'Cached'
        this.forecastRevision += 1
      } else if (this.forecast.spotId && this.forecast.spotId !== spot.id) {
        this.forecastSource = 'previous'
        this.forecastLabel = 'Previous spot'
      }
      this.forecastStatus = 'loading'
      this.forecastMessage = `Loading current forecast for ${spot.name}…`

      try {
        const forecasts = await fetcher(spot, fetchOptions)
        if (requestId !== this.forecastRequestId || this.selectedSpotId !== spot.id) return false
        if (!forecasts?.[this.selectedModelId]) this.selectedModelId = DEFAULT_FORECAST_MODEL_ID
        const nextForecast = forecasts?.[this.selectedModelId]
        if (!nextForecast || nextForecast.spotId !== spot.id) {
          throw new Error('The selected forecast model is unavailable.')
        }
        writeCachedForecasts(forecasts, storage)
        this.forecastsByModel = forecasts
        this.forecast = nextForecast
        this.forecastRevision += 1
        this.pendingForecastRevision = this.forecastRevision
        this.pendingForecastSpotId = spot.id
        this.pendingForecastModelId = this.selectedModelId
        this.pendingForecastSource = 'current'
        this.forecastSource = 'current'
        this.forecastStatus = 'rendering'
        this.forecastMessage = `Updating the ${spot.name} preview…`
        return true
      } catch {
        if (requestId !== this.forecastRequestId || this.selectedSpotId !== spot.id) return false
        if (this.pendingForecastSource !== 'cache') {
          this.pendingForecastRevision = null
          this.pendingForecastSpotId = null
          this.pendingForecastModelId = null
          this.pendingForecastSource = null
        }
        this.forecastStatus = 'warning'
        if (this.forecastSource === 'demo') {
          this.forecastLabel = 'Demo'
          this.forecastMessage = 'Live forecast unavailable. Showing demo data.'
        } else if (this.forecastSource === 'previous') {
          const previousSpot = this.forecast.spotName ?? this.forecast.spot ?? 'the previous spot'
          this.forecastMessage = `Could not load ${spot.name}. Still showing ${previousSpot}.`
        } else if (this.forecast.spotId === spot.id && this.forecast.modelId !== this.selectedModelId) {
          const requestedModel = getForecastModel(this.selectedModelId)
          this.forecastSource = 'previous'
          this.forecastLabel = 'Previous model'
          this.forecastMessage = `Could not load ${requestedModel?.label ?? 'that model'}. Still showing ${this.forecast.model}.`
        } else if (this.forecastSource === 'cache') {
          this.forecastLabel = 'Cached'
          this.forecastMessage = 'Could not refresh. Showing the cached forecast.'
        } else {
          this.forecastLabel = 'Update delayed'
          this.forecastMessage = 'Could not refresh. Showing the last forecast.'
        }
        return false
      } finally {
        if (requestId === this.forecastRequestId) this.forecastRequestInFlight = false
      }
    },
    async selectSpot(spotId, { tideFetcher, swellFetcher, ...options } = {}) {
      const spot = this.spotById(spotId)
      if (!spot) return false
      if (spotId === this.selectedSpotId) return true
      this.selectedSpotId = spotId
      this.swellRequestId += 1
      this.swell = null
      this.swellStatus = 'idle'
      if (this.swellFocus) void this.refreshSwell({ fetcher: swellFetcher ?? fetchOpenMeteoSwell })
      if (!forecastModelsForSpot(spot).some((model) => model.id === this.selectedModelId)) {
        this.selectedModelId = DEFAULT_FORECAST_MODEL_ID
      }
      const forecastResult = this.refreshForecast(options)
      if (this.tideInitialized) {
        void this.refreshTide({
          fetcher: tideFetcher ?? fetchOpenMeteoTide,
          storage: options.storage,
        })
      }
      return forecastResult
    },
    async selectModel(modelId, { storage, ...refreshOptions } = {}) {
      const model = getForecastModel(modelId)
      if (!model || !this.availableForecastModels.some((candidate) => candidate.id === modelId)) return false
      if (modelId === this.selectedModelId) return true
      const requestInFlight = this.forecastRequestInFlight
      this.selectedModelId = modelId
      this.pendingForecastRevision = null
      this.pendingForecastSpotId = null
      this.pendingForecastModelId = null
      this.pendingForecastSource = null

      const current = this.forecastsByModel[modelId]
      const cached = current ? null : readCachedForecast(this.selectedSpotId, modelId, storage)
      const available = current ?? cached
      if (!available && requestInFlight) {
        const spot = this.spotById(this.selectedSpotId)
        this.forecastMessage = `Loading ${model.label} forecast for ${spot?.name ?? 'this spot'}…`
        return true
      }
      if (!available || available.spotId !== this.selectedSpotId) {
        return this.refreshForecast({ storage, ...refreshOptions })
      }

      this.forecastsByModel[modelId] = available
      this.forecast = available
      this.forecastRevision += 1
      this.pendingForecastRevision = this.forecastRevision
      this.pendingForecastSpotId = this.selectedSpotId
      this.pendingForecastModelId = modelId
      this.pendingForecastSource = cached ? 'cache' : 'current'
      this.forecastSource = this.pendingForecastSource
      this.forecastLabel = cached ? 'Cached' : this.forecastLabel
      this.forecastStatus = 'rendering'
      this.forecastMessage = `Updating the ${cached ? 'cached ' : ''}${model.label} preview…`
      return true
    },
    publishForecast(revision) {
      if (revision !== this.pendingForecastRevision || revision !== this.forecastRevision ||
          this.pendingForecastSpotId !== this.selectedSpotId ||
          this.pendingForecastModelId !== this.selectedModelId ||
          this.forecast.spotId !== this.selectedSpotId ||
          this.forecast.modelId !== this.selectedModelId) return false
      const spot = this.spotById(this.selectedSpotId)
      const model = getForecastModel(this.selectedModelId)
      const publicationSource = this.pendingForecastSource
      this.pendingForecastRevision = null
      this.pendingForecastSpotId = null
      this.pendingForecastModelId = null
      this.pendingForecastSource = null
      this.publishedForecast = this.forecast
      if (publicationSource === 'cache') {
        this.forecastSource = 'cache'
        this.forecastLabel = 'Cached'
        if (this.forecastStatus !== 'warning') {
          this.forecastStatus = this.forecastRequestInFlight ? 'loading' : 'ready'
          this.forecastMessage = this.forecastRequestInFlight
            ? `Cached ${model?.label ?? 'model'} forecast for ${spot?.name ?? 'this spot'}. Refreshing current data…`
            : `Cached ${model?.label ?? 'model'} forecast for ${spot?.name ?? 'this spot'}.`
        }
        return true
      }
      this.forecastSource = 'live'
      this.forecastLabel = ''
      this.forecastStatus = 'ready'
      this.forecastMessage = `Live ${model?.label ?? 'model'} forecast for ${spot?.name ?? 'this spot'}.`
      return true
    },
    rejectForecastPublication(revision) {
      if (revision !== this.pendingForecastRevision || revision !== this.forecastRevision) return false
      const failedSpot = this.spotById(this.pendingForecastSpotId)
      const failedModel = getForecastModel(this.pendingForecastModelId)
      this.pendingForecastRevision = null
      this.pendingForecastSpotId = null
      this.pendingForecastModelId = null
      this.pendingForecastSource = null
      this.forecast = this.publishedForecast
      this.forecastRevision += 1
      this.forecastStatus = 'warning'
      if (this.forecast.spotId !== this.selectedSpotId) {
        this.forecastSource = 'previous'
        this.forecastLabel = 'Previous spot'
        this.forecastMessage = `Could not show ${failedSpot?.name ?? 'that spot'}. Still showing ${this.forecast.spotName}.`
      } else if (this.forecast.modelId !== this.selectedModelId) {
        this.forecastSource = 'previous'
        this.forecastLabel = 'Previous model'
        this.forecastMessage = `Could not show ${failedModel?.label ?? 'that model'}. Still showing ${this.forecast.model}.`
      } else {
        this.forecastSource = 'current'
        this.forecastLabel = 'Update delayed'
        this.forecastMessage = 'Could not update the preview. Showing the last forecast.'
      }
      return true
    },
  },
})
