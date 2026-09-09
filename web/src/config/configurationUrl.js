import { MIN_THRESHOLD, MAX_THRESHOLD } from '../renderer/contract'
import { BOARD_IDS, TIME_FORMATS, TEMPERATURE_UNITS } from './configuration'
import { validModuleOrder } from './modules'
import { forecastModelsForSpot } from '../forecast/models'
import { SWELL_MODELS } from '../forecast/openMeteoSwell'
import { createPersonalSpot, writePersonalSpot } from '../spots/personalSpots'

const sizes = { hide: 'off', numbers: 'small', graph: 'large' }
const flags = { weather: 'showWeather', temperature: 'showTemperature', tide: 'showTide', footer: 'showDedicatedFooter', threshold: 'showThreshold' }
const keys = ['swell', 'cfg', 'spot', 'custom', 'board', 'wind', 'waves', 'wind-model', 'wave-model', 'order', 'minimum', 'time', 'unit', ...Object.keys(flags)]

// Versioned, explicit fields only. No installer credentials or runtime state enter the URL.
export function configurationUrl(store, href) {
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
  return url
}

export function readConfigurationUrl(search, spots) {
  try {
    const params = new URLSearchParams(search)
    if (params.get('cfg') !== '1' || search.length > 5000 || keys.some(key => params.getAll(key).length > 1)) return null
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
    return { patch, spot }
  } catch { return null }
}

export function applyConfigurationUrl(store, search, storage) {
  const result = readConfigurationUrl(search, store.spots)
  if (!result) return false
  if (result.spot.personal) {
    const remaining = store.personalSpots.filter(spot => spot.id !== result.spot.id)
    store.personalSpots = [...remaining, result.spot]
    writePersonalSpot(result.spot, storage)
  }
  store.$patch({ ...result.patch, swellFocus: result.patch.swellSize !== 'off' })
  return true
}

export function syncConfigurationUrl(store, browser = window) {
  const update = () => {
    const url = configurationUrl(store, browser.location.href)
    if (url && url.href !== browser.location.href) browser.history.replaceState(browser.history.state, '', url.href)
  }
  update()
  return store.$subscribe(update, { detached: true, flush: 'post' })
}
