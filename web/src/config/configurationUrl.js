import { watch } from 'vue'
import { MIN_THRESHOLD, MAX_THRESHOLD } from '../renderer/contract'
import { BOARD_IDS, TIME_FORMATS, TEMPERATURE_UNITS } from './configuration'
import { validModuleOrder } from './modules'
import { forecastModelsForSpot } from '../forecast/models'
import { SWELL_MODELS } from '../forecast/openMeteoSwell'
import { createPersonalSpot, writePersonalSpots } from '../spots/personalSpots'
import { settingsForSpot } from './spotSettings'

const sizes = { hide: 'off', numbers: 'small', graph: 'large' }
const flags = { weather: 'showWeather', temperature: 'showTemperature', tide: 'showTide', footer: 'showDedicatedFooter', threshold: 'showThreshold' }
const keys = ['spots', 'swell', 'cfg', 'spot', 'custom', 'board', 'wind', 'waves', 'wind-model', 'wave-model', 'order', 'minimum', 'time', 'unit', ...Object.keys(flags)]

// Versioned, explicit fields only. No installer credentials or runtime state enter the URL.
export function configurationUrl(store, href, { includeSpots = true } = {}) {
  const url = new URL(href)
  for (const key of keys) url.searchParams.delete(key)
  url.searchParams.set('configure', '')
  url.searchParams.set('cfg', '1')
  const spot = store.spotById(store.selectedSpotId)
  if (!spot) return null
  if (spot.personal) {
    url.searchParams.set('custom', JSON.stringify({ name: spot.name, latitude: spot.latitude, longitude: spot.longitude, timezone: spot.timezone, countryCode: spot.countryCode }))
  } else url.searchParams.set('spot', spot.id)
  url.searchParams.set('board', Object.keys(BOARD_IDS).find(key => BOARD_IDS[key] === store.selectedBoardId))
  for (const [param, field] of [['wind', 'windSize'], ['waves', 'swellSize']]) {
    url.searchParams.set(param, Object.keys(sizes).find(key => sizes[key] === store[field]))
  }
  url.searchParams.set('wind-model', store.selectedModelId)
  url.searchParams.set('wave-model', store.selectedSwellModelId)
  url.searchParams.set('order', store.moduleOrder.join(','))
  url.searchParams.set('minimum', String(store.threshold))
  url.searchParams.set('time', store.timeFormat)
  url.searchParams.set('unit', store.temperatureUnit)
  for (const [param, field] of Object.entries(flags)) url.searchParams.set(param, store[field] ? '1' : '0')
  if (includeSpots && Array.isArray(store.configuredSpotIds)) {
    const entries = store.configuredSpotIds.map(id => {
      const draft = {
        ...settingsForSpot(store, id), selectedSpotId: id,
        selectedBoardId: store.selectedBoardId, spotById: store.spotById,
      }
      return configurationUrl(draft, href, { includeSpots: false })?.search
    })
    if (entries.some(entry => !entry)) return null
    url.searchParams.set('spots', JSON.stringify(entries))
  }
  return url
}

export function readConfigurationUrl(search, spots, { allowSpots = true } = {}) {
  try {
    const params = new URLSearchParams(search)
    if (params.get('cfg') !== '1' || search.length > (allowSpots ? 50000 : 5000) || keys.some(key => params.getAll(key).length > 1)) return null
    if (!allowSpots && params.has('spots')) return null
    const custom = params.get('custom')
    if (custom && params.has('spot')) return null
    let spot
    if (custom) {
      const input = JSON.parse(custom)
      if (!input || typeof input.name !== 'string' || typeof input.latitude !== 'number' || typeof input.longitude !== 'number') return null
      spot = createPersonalSpot(input)
    } else spot = spots.find(candidate => candidate.id === params.get('spot'))
    if (!spot || !Object.hasOwn(sizes, params.get('wind')) || !Object.hasOwn(sizes, params.get('waves')) || !Object.hasOwn(BOARD_IDS, params.get('board'))) return null
    const model = params.get('wind-model')
    if (!forecastModelsForSpot(spot).some(candidate => candidate.id === model)) return null
    if (!SWELL_MODELS.some(candidate => candidate.value === params.get('wave-model'))) return null
    const order = params.get('order')?.split(',')
    if (!validModuleOrder(order) || !/^\d{1,2}$/.test(params.get('minimum') ?? '')) return null
    if (Number(params.get('minimum')) < MIN_THRESHOLD || Number(params.get('minimum')) > MAX_THRESHOLD) return null
    if (!TIME_FORMATS.includes(params.get('time')) || !TEMPERATURE_UNITS.includes(params.get('unit'))) return null
    const patch = {
      selectedSpotId: spot.id, hasUserSpotIntent: true,
      selectedBoardId: BOARD_IDS[params.get('board')],
      windSize: sizes[params.get('wind')], swellSize: sizes[params.get('waves')],
      selectedModelId: model, selectedSwellModelId: params.get('wave-model'),
      moduleOrder: order, threshold: Number(params.get('minimum')),
      timeFormat: params.get('time'), temperatureUnit: params.get('unit'), temperatureUnitInitialized: true,
    }
    for (const [param, field] of Object.entries(flags)) {
      if (!['0', '1'].includes(params.get(param))) return null
      patch[field] = params.get(param) === '1'
    }
    const personalSpots = []
    if (allowSpots) {
      patch.configuredSpotIds = [spot.id]
      patch.spotSettings = {}
      if (params.has('spots')) {
        const entries = JSON.parse(params.get('spots'))
        if (!Array.isArray(entries) || entries.length > 10) return null
        patch.configuredSpotIds = []
        for (const entry of entries) {
          if (typeof entry !== 'string') return null
          const result = readConfigurationUrl(entry, spots, { allowSpots: false })
          if (!result || patch.configuredSpotIds.includes(result.spot.id)) return null
          patch.configuredSpotIds.push(result.spot.id)
          const { selectedSpotId, selectedBoardId, hasUserSpotIntent, ...settings } = result.patch
          patch.spotSettings[result.spot.id] = settings
          if (result.spot.personal) personalSpots.push(result.spot)
        }
        if (patch.selectedBoardId === BOARD_IDS.E1003 && entries.length && !patch.configuredSpotIds.includes(spot.id)) return null
      }
    }
    return { patch, spot, personalSpots }
  } catch { return null }
}

export function applyConfigurationUrl(store, search, storage) {
  const result = readConfigurationUrl(search, store.spots)
  if (!result) return false
  // The active personal spot also occurs in the multi-spot list. Import it once,
  // using the top-level definition, which owns the active preview.
  const imported = result.personalSpots.filter(spot => spot.id !== result.spot.id)
  if (result.spot.personal) imported.push(result.spot)
  writePersonalSpots(imported, storage)
  store.$patch(state => {
    if (imported.length) {
      const ids = new Set(imported.map(spot => spot.id))
      state.personalSpots = [...state.personalSpots.filter(spot => !ids.has(spot.id)), ...imported]
    }
    // Replace the dictionary: an object patch would deep-merge the previous draft.
    Object.assign(state, result.patch, { swellFocus: result.patch.swellSize !== 'off' })
  })
  return true
}

export function syncConfigurationUrl(store, browser = window) {
  return watch(
    () => configurationUrl(store, browser.location.href)?.href,
    href => {
      if (href && href !== browser.location.href) browser.history.replaceState(browser.history.state, '', href)
    },
    { immediate: true, flush: 'post' },
  )
}
