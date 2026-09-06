import { isNormalizedForecast } from './normalizeForecast'
import { availableStorage } from '../storage'

export const FORECAST_CACHE_KEY = 'windscout.forecasts'
export const FORECAST_CACHE_VERSION = 3

function readEnvelope(storage) {
  const target = availableStorage(storage)
  if (!target) return null
  try {
    const envelope = JSON.parse(target.getItem(FORECAST_CACHE_KEY))
    if (!envelope || envelope.version !== FORECAST_CACHE_VERSION ||
        !envelope.spots || typeof envelope.spots !== 'object' || Array.isArray(envelope.spots)) return null
    return envelope
  } catch {
    return null
  }
}

export function readCachedForecast(spotId, modelId, storage) {
  const forecast = readEnvelope(storage)?.spots?.[spotId]?.[modelId]
  return isNormalizedForecast(forecast) && forecast.spotId === spotId &&
    forecast.modelId === modelId ? forecast : null
}

export function writeCachedForecasts(forecasts, storage) {
  const values = Array.isArray(forecasts) ? forecasts : Object.values(forecasts ?? {})
  if (!values.length || !values.every(isNormalizedForecast)) return false
  const target = availableStorage(storage)
  if (!target) return false
  try {
    const envelope = readEnvelope(target) ?? { version: FORECAST_CACHE_VERSION, spots: {} }
    values.forEach((forecast) => {
      envelope.spots[forecast.spotId] ??= {}
      envelope.spots[forecast.spotId][forecast.modelId] = forecast
    })
    target.setItem(FORECAST_CACHE_KEY, JSON.stringify(envelope))
    return true
  } catch {
    return false
  }
}
