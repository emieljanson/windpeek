import { MIN_THRESHOLD, MAX_THRESHOLD } from '../renderer/contract'
import { SWELL_MODELS } from '../forecast/openMeteoSwell'
import { availableStorage } from '../storage'
import { MODULE_SIZES, validModuleOrder } from './modules'
import { SUPPORTED_BOARD_IDS, TIME_FORMATS, TEMPERATURE_UNITS } from './configuration'
import { forecastModelsForSpot } from '../forecast/models'

const KEY = 'windpeek-configurator-v1'
const booleans = ['showThreshold', 'showWeather', 'showTemperature', 'showTide', 'showDedicatedFooter']
const fields = [...booleans, 'threshold', 'windSize', 'swellSize', 'moduleOrder', 'selectedBoardId', 'selectedSpotId', 'selectedModelId', 'selectedSwellModelId', 'timeFormat', 'temperatureUnit', 'temperatureUnitInitialized']

export function persistConfigurator({ store }, storage = availableStorage(), { subscribe = true, restore = true } = {}) {
  if (store.$id !== 'configurator' || !storage) return
  try {
    const saved = restore ? JSON.parse(storage.getItem(KEY)) : null
    if (saved && typeof saved === 'object') {
      const patch = {}
      for (const key of booleans) if (typeof saved[key] === 'boolean') patch[key] = saved[key]
      for (const key of ['windSize', 'swellSize']) if (MODULE_SIZES.includes(saved[key])) patch[key] = saved[key]
      if (validModuleOrder(saved.moduleOrder)) patch.moduleOrder = [...saved.moduleOrder]
      if (Number.isInteger(saved.threshold) && saved.threshold >= MIN_THRESHOLD && saved.threshold <= MAX_THRESHOLD) patch.threshold = saved.threshold
      if (SUPPORTED_BOARD_IDS.includes(saved.selectedBoardId)) patch.selectedBoardId = saved.selectedBoardId
      if (TIME_FORMATS.includes(saved.timeFormat)) patch.timeFormat = saved.timeFormat
      if (TEMPERATURE_UNITS.includes(saved.temperatureUnit)) {
        patch.temperatureUnit = saved.temperatureUnit
        patch.temperatureUnitInitialized = saved.temperatureUnitInitialized !== false
      }
      if (patch.moduleOrder && saved.schemaVersion !== 2) {
        patch.moduleOrder = patch.moduleOrder.filter(id => id !== 'temperature')
        patch.moduleOrder.splice(patch.moduleOrder.indexOf('weather') + 1, 0, 'temperature')
      }
      const spot = store.spots.find(spot => spot.id === saved.selectedSpotId)
      if (spot) {
        patch.selectedSpotId = spot.id
        patch.hasUserSpotIntent = true
        if (forecastModelsForSpot(spot).some(model => model.id === saved.selectedModelId)) patch.selectedModelId = saved.selectedModelId
      }
      if (SWELL_MODELS.some(model => model.value === saved.selectedSwellModelId)) patch.selectedSwellModelId = saved.selectedSwellModelId
      store.$patch(patch)
      store.swellFocus = store.swellSize !== 'off'
    }
  } catch { /* Storage may be blocked or contain an obsolete draft. */ }
  if (!subscribe) return
  store.$subscribe((_mutation, state) => {
    try { storage.setItem(KEY, JSON.stringify({ schemaVersion: 2, ...Object.fromEntries(fields.map(key => [key, state[key]])) })) }
    catch { /* A full/blocked store must not prevent configuration. */ }
  }, { detached: true, flush: 'sync' })
}
