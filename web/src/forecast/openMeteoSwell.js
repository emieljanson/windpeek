export const SWELL_MODELS = Object.freeze([
  { value: 'best_match', label: 'Best Match' },
  { value: 'meteofrance_wave', label: 'MFWAM' },
  { value: 'ncep_gfswave025', label: 'GFS Wave' },
  { value: 'dwd_ewam', label: 'EWAM' },
])
import { OPEN_METEO_MARINE_ENDPOINT, OPEN_METEO_MARINE_TIMEOUT_MS } from './openMeteoMarine'

const fields = ['swell_wave_height', 'swell_wave_period', 'swell_wave_direction']
const secondaryFields = ['secondary_swell_wave_height', 'secondary_swell_wave_period', 'secondary_swell_wave_direction']
const hours = ['08', '11', '14', '17', '20']

export function normalizeSwell(response, spot, { retrievedAt = Date.now() } = {}) {
  const hourly = response?.hourly
  if (response?.timezone !== spot.timezone || !Array.isArray(hourly?.time) ||
      fields.some((field) => !Array.isArray(hourly[field]) || hourly[field].length !== hourly.time.length) ||
      response.hourly_units?.swell_wave_height !== 'm' ||
      response.hourly_units?.swell_wave_period !== 's' ||
      response.hourly_units?.swell_wave_direction !== '°') throw new Error('Invalid swell forecast')
  const secondaryValid = secondaryFields.every((field, index) =>
    Array.isArray(hourly[field]) && hourly[field].length === hourly.time.length &&
    response.hourly_units?.[field] === ['m', 's', '°'][index])
  let previous = ''
  const samples = hourly.time.flatMap((time, index) => {
    if (typeof time !== 'string' || !/^\d{4}-\d{2}-\d{2}T\d{2}:00$/.test(time) || time <= previous) {
      throw new Error('Invalid swell forecast times')
    }
    previous = time
    const [height, period, direction] = fields.map((field) => hourly[field][index])
    const [secondaryHeight, secondaryPeriod, secondaryDirection] = secondaryValid ? secondaryFields.map(field => hourly[field][index]) : []
    return [{
      secondaryHeightCm: Number.isFinite(secondaryHeight) && secondaryHeight >= 0 && secondaryHeight <= 100 ? Math.round(secondaryHeight * 100) : -1,
      secondaryPeriodTenths: Number.isFinite(secondaryPeriod) && secondaryPeriod > 0 && secondaryPeriod <= 100 ? Math.round(secondaryPeriod * 10) : -1,
      secondaryDestinationDegrees: Number.isFinite(secondaryDirection) && secondaryDirection >= 0 && secondaryDirection <= 360 ? Math.round((secondaryDirection + 180) % 360) % 360 : -1,
      localDate: time.slice(0, 10),
      time: time.slice(11, 13),
      heightCm: Number.isFinite(height) && height >= 0 && height <= 100 ? Math.round(height * 100) : -1,
      periodTenths: Number.isFinite(period) && period > 0 && period <= 100 ? Math.round(period * 10) : -1,
      destinationDegrees: Number.isFinite(direction) && direction >= 0 && direction <= 360
        ? Math.round((direction + 180) % 360) % 360 : -1,
    }]
  })
  return { spotId: spot.id, spotName: spot.displayName ?? spot.name, timezone: spot.timezone,
    retrievedAt, hourly: samples, samples: samples.filter((sample) => hours.includes(sample.time)),
    available: samples.some((sample) => sample.heightCm >= 0) }
}

// Keep the stored GFS id compatible with existing installations; it now selects
// the family. Open-Meteo exposes 16 km between 15°S and 52.5°N, and 25 km globally.
export function swellModelCandidates(model, latitude) {
  if (model === 'dwd_ewam') return ['dwd_ewam', 'dwd_gwam']
  return model === 'ncep_gfswave025' && latitude >= -15 && latitude <= 52.5
    ? ['ncep_gfswave016', 'ncep_gfswave025'] : [model]
}

function completeSwellSample(sample) {
  return sample.heightCm === 0 ||
    (sample.heightCm > 0 && sample.periodTenths > 0 && sample.destinationDegrees >= 0)
}

export async function fetchOpenMeteoSwell(spot, options = {}) {
  const model = options.model ?? 'best_match'
  if (!SWELL_MODELS.some(option => option.value === model)) throw new Error('Unknown swell model')
  const candidates = swellModelCandidates(model, spot.latitude)
  const results = await Promise.allSettled(candidates.map(candidate => fetchSwellModel(spot, {...options, model: candidate})))
  const forecasts = results.filter(result => result.status === 'fulfilled').map(result => result.value)
  if (!forecasts.length) throw results[0].reason
  const samples = new Map()
  // Coarse first, then overwrite with complete fine-grid samples. Never combine
  // height, direction, period or secondary components from different sources.
  for (const forecast of [...forecasts].reverse()) {
    for (const sample of forecast.hourly) {
      const key = `${sample.localDate}T${sample.time}`
      if (!samples.has(key) || completeSwellSample(sample)) samples.set(key, sample)
    }
  }
  const hourly = [...samples.values()].sort((a, b) => `${a.localDate}${a.time}`.localeCompare(`${b.localDate}${b.time}`))
  return {...forecasts[0], model, hourly, samples: hourly.filter(sample => hours.includes(sample.time)),
    retrievedAt: Math.min(...forecasts.map(forecast => forecast.retrievedAt)),
    available: hourly.some(sample => sample.heightCm >= 0)}
}

async function fetchSwellModel(spot, {
  model = 'best_match', fetchImpl = globalThis.fetch, timeoutMs = OPEN_METEO_MARINE_TIMEOUT_MS, now = Date.now,
} = {}) {
  const requestedFields = model === 'dwd_ewam' || model === 'dwd_gwam' ? fields : [...fields, ...secondaryFields]
  const params = new URLSearchParams({ models: model, latitude: spot.latitude, longitude: spot.longitude,
    hourly: requestedFields.join(','), timezone: spot.timezone, forecast_days: '5', cell_selection: 'sea' })
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  try {
    const response = await fetchImpl(`${OPEN_METEO_MARINE_ENDPOINT}?${params}`, {
      signal: controller.signal, headers: { Accept: 'application/json' },
    })
    if (!response.ok) throw new Error(`Swell forecast request failed (${response.status})`)
    return { ...normalizeSwell(await response.json(), spot, { retrievedAt: now() }), model }
  } finally { clearTimeout(timer) }
}

// Swell owns its calendar. Never pair a new spot's swell with the previous
// spot's wind or let a failed wind request prevent the swell preview.
export function swellDashboardForecast(wind, swell, weatherRequired = true) {
  if (!swell?.samples.length) return wind
  const dates = [...new Set(swell.samples.map((sample) => sample.localDate))].slice(0, 5)
  if (dates.length !== 5) return wind
  const sameSpot = wind.spotId === swell.spotId && wind.timezone === swell.timezone
  const days = dates.map((localDate, index) => {
    const windDay = sameSpot ? wind.days.find((day) => day.localDate === localDate) : null
    const date = new Date(`${localDate}T12:00:00Z`)
    return { localDate,
      day: index === 0 ? 'TODAY' : date.toLocaleDateString('en-GB', { weekday: 'long', timeZone: 'UTC' }).toUpperCase(),
      date: date.toLocaleDateString('en-GB', { day: '2-digit', month: 'short', timeZone: 'UTC' }).toUpperCase(),
      samples: hours.map((time) => windDay?.samples.find((sample) => sample.time === time) ?? {
        time, sustainedKt: 0, gustKt: 0, destinationDegrees: 0, available: false,
        weather: 0, temperatureTenthsC: 0, temperatureAvailable: false,
      }),
    }
  })
  return { ...wind, spotId: swell.spotId, spotName: swell.spotName, timezone: swell.timezone,
    days, retrievedAt: weatherRequired && sameSpot && Number.isFinite(wind.retrievedAt)
      ? Math.min(wind.retrievedAt, swell.retrievedAt) : swell.retrievedAt,
    model: 'OPEN-METEO', state: weatherRequired && sameSpot && wind.state !== 3 ? (wind.state ?? 0) : 0,
    refreshFailed: weatherRequired && (!sameSpot || wind.refreshFailed || wind.state === 3) }
}
