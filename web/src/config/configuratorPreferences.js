import { watch } from 'vue'
import { MIN_THRESHOLD, MAX_THRESHOLD } from '../renderer/contract'
import { SWELL_MODELS } from '../forecast/openMeteoSwell'
import { availableStorage } from '../storage'
import { MODULE_SIZES, validModuleOrder } from './modules'
import { BOARD_IDS, SUPPORTED_BOARD_IDS, TIME_FORMATS, TEMPERATURE_UNITS } from './configuration'
import { forecastModelsForSpot } from '../forecast/models'

const KEY = 'windpeek-configurator-v1'
const OLD_SWELL_DEFAULT_ORDER = 'swell,wind,weather,temperature,tide'
const booleans = ['showThreshold', 'showWeather', 'showTemperature', 'showTide', 'showDedicatedFooter']
const fields = ['configuredSpotIds', 'spotSettings', ...booleans, 'threshold', 'windSize', 'swellSize', 'moduleOrder', 'selectedBoardId', 'selectedSpotId', 'selectedModelId', 'selectedSwellModelId', 'timeFormat', 'temperatureUnit', 'temperatureUnitInitialized']

export function persistConfigurator({ store }, storage = availableStorage(), { subscribe = true, restore = true } = {}) {
  if (store.$id !== 'configurator' || !storage) return
  try {
    const saved = restore ? JSON.parse(storage.getItem(KEY)) : null
    if (saved && typeof saved === 'object') {
      const patch = readPreferencePatch(saved, store)
      if (Array.isArray(saved.configuredSpotIds)) {
        patch.configuredSpotIds = [...new Set(saved.configuredSpotIds)].filter(id => store.spotById(id)).slice(0, 10)
        patch.spotSettings = Object.fromEntries(patch.configuredSpotIds.map(id => {
          const settings = readPreferencePatch({ ...saved.spotSettings?.[id], selectedSpotId: id, schemaVersion: 2 }, store)
          delete settings.selectedSpotId
          delete settings.hasUserSpotIntent
          delete settings.selectedBoardId
          return [id, settings]
        }))
        if (patch.configuredSpotIds.length && !patch.configuredSpotIds.includes(patch.selectedSpotId) && (patch.selectedBoardId ?? store.selectedBoardId) === BOARD_IDS.E1003) {
          patch.selectedSpotId = patch.configuredSpotIds[0]
          Object.assign(patch, patch.spotSettings[patch.selectedSpotId])
        }
      } else if (patch.selectedSpotId) patch.configuredSpotIds = [patch.selectedSpotId]
      store.$patch(patch)
      store.swellFocus = store.swellSize !== 'off'
    }
  } catch { /* Storage may be blocked or contain an obsolete draft. */ }
  if (!subscribe) return
  return watch(
    () => Object.fromEntries(fields.map(key => [key, store[key]])),
    preferences => {
      try { storage.setItem(KEY, JSON.stringify({ schemaVersion: 2, ...preferences })) }
      catch { /* A full/blocked store must not prevent configuration. */ }
    },
    { deep: true, flush: 'sync' },
  )
}

function readPreferencePatch(saved, store) {
  const patch = {}
  for (const key of booleans) if (typeof saved[key] === 'boolean') patch[key] = saved[key]
  for (const key of ['windSize', 'swellSize']) if (MODULE_SIZES.includes(saved[key])) patch[key] = saved[key]
  if (validModuleOrder(saved.moduleOrder)) {
    patch.moduleOrder = saved.moduleOrder.join(',') === OLD_SWELL_DEFAULT_ORDER
      ? ['wind', 'swell', 'weather', 'temperature', 'tide']
      : [...saved.moduleOrder]
  }
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
  return patch
}
