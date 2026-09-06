import { RENDERER_TEXT_CAPACITIES, textFitsRenderer } from '../renderer/contract'
import { deviceTimezone as resolveDeviceTimezone, validTimezone } from '../timezone'
import { FORECAST_MODELS, getForecastModel } from './models'

const REQUIRED_HOURS = Object.freeze([8, 11, 14, 17, 20])
const CORE_WIND_FIELDS = Object.freeze([
  'wind_speed_10m',
  'wind_gusts_10m',
  'wind_direction_10m',
])
const DAY_NAMES = Object.freeze([
  'SUNDAY', 'MONDAY', 'TUESDAY', 'WEDNESDAY', 'THURSDAY', 'FRIDAY', 'SATURDAY',
])
const MONTH_NAMES = Object.freeze([
  'JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN',
  'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC',
])

function fail(message) {
  throw new Error(`Invalid Open-Meteo forecast: ${message}`)
}

function dateParts(date) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(date)
  if (!match) fail(`invalid local date ${date}`)
  const year = Number(match[1])
  const month = Number(match[2])
  const day = Number(match[3])
  const value = new Date(Date.UTC(year, month - 1, day))
  if (value.getUTCFullYear() !== year || value.getUTCMonth() !== month - 1 || value.getUTCDate() !== day) {
    fail(`invalid local date ${date}`)
  }
  return { year, month, day, value }
}

function addDays(date, amount) {
  const { value } = dateParts(date)
  value.setUTCDate(value.getUTCDate() + amount)
  return `${value.getUTCFullYear()}-${String(value.getUTCMonth() + 1).padStart(2, '0')}-${String(value.getUTCDate()).padStart(2, '0')}`
}

function localDateAt(timestamp, timezone) {
  const parts = new Intl.DateTimeFormat('en-CA', {
    timeZone: timezone,
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
  }).formatToParts(new Date(timestamp))
  const value = Object.fromEntries(parts.map((part) => [part.type, part.value]))
  return `${value.year}-${value.month}-${value.day}`
}

function updatedTimeAt(timestamp, timezone) {
  const parts = new Intl.DateTimeFormat('en-US', {
    timeZone: timezone,
    day: '2-digit',
    month: 'short',
    hour: 'numeric',
    hour12: true,
  }).formatToParts(new Date(timestamp))
  const values = Object.fromEntries(parts.map((part) => [part.type, part.value]))
  return `${values.day} ${values.month.toUpperCase()} ${values.hour}${values.dayPeriod.toUpperCase()}`
}

function weatherState(cloudCover, precipitation, isDay) {
  if (![cloudCover, precipitation, isDay].every(Number.isFinite) ||
      cloudCover < 0 || cloudCover > 100 || precipitation < 0 || precipitation > 655.35 ||
      (isDay !== 0 && isDay !== 1)) return 0
  const precipitationHundredths = Math.round(precipitation * 100)
  const cloudCoverPercent = Math.round(cloudCover)
  if (precipitationHundredths >= 250) return 8
  if (precipitationHundredths >= 100) return 7
  if (precipitationHundredths >= 10) return 6
  if (cloudCoverPercent <= 20) return isDay ? 1 : 2
  if (cloudCoverPercent <= 60) return isDay ? 3 : 4
  return 5
}

function modelField(field, model, suffixed) {
  return suffixed ? `${field}_${model.apiId}` : field
}

function roundSymmetric(value) {
  return value < 0 ? -Math.round(-value) : Math.round(value)
}

function responseFields(model, suffixed) {
  return {
    wind: modelField('wind_speed_10m', model, suffixed),
    gust: modelField('wind_gusts_10m', model, suffixed),
    direction: modelField('wind_direction_10m', model, suffixed),
    cloud: modelField('cloud_cover', model, suffixed),
    precipitation: modelField('precipitation', model, suffixed),
    isDay: modelField('is_day', model, suffixed),
    temperature: modelField('temperature_2m', model, suffixed),
  }
}

function validateModelFields(hourly, units, count, model, suffixed) {
  const fields = responseFields(model, suffixed)
  if (units[fields.wind] !== 'kn' || units[fields.gust] !== 'kn' ||
      units[fields.direction] !== '\u00b0') fail('wind units do not match the renderer contract')
  if (CORE_WIND_FIELDS.some((field) => {
    const key = modelField(field, model, suffixed)
    return !Array.isArray(hourly[key]) || hourly[key].length !== count
  })) fail('core hourly arrays are missing or misaligned')
  return fields
}

function validateCoreResponse(response, model, suffixed, timezone, fallbackModel) {
  if (!response || typeof response !== 'object') fail('response is missing')
  if (response.timezone !== timezone) fail(`timezone must be ${timezone}`)
  const units = response.hourly_units
  if (!units || typeof units !== 'object') fail('hourly units are missing')
  const hourly = response.hourly
  if (!hourly || typeof hourly !== 'object') fail('hourly data is missing')
  const count = Array.isArray(hourly.time) ? hourly.time.length : 0
  if (!count) fail('hourly times are missing')
  const fields = validateModelFields(hourly, units, count, model, suffixed)
  const fallbackFields = fallbackModel
    ? validateModelFields(hourly, units, count, fallbackModel, suffixed)
    : null
  return { hourly, units, count, fields, fallbackFields }
}

function optionalAvailability(hourly, units, count, fields) {
  return {
    weather: [fields.cloud, fields.precipitation, fields.isDay]
      .every((field) => Array.isArray(hourly[field]) && hourly[field].length === count) &&
      units[fields.cloud] === '%' && units[fields.precipitation] === 'mm',
    temperature: Array.isArray(hourly[fields.temperature]) &&
      hourly[fields.temperature].length === count && units[fields.temperature] === '°C',
  }
}

export function normalizeForecast(response, spot, {
  retrievedAt = Date.now(),
  firstDate = localDateAt(retrievedAt, spot?.timezone ?? 'Europe/Amsterdam'),
  deviceTimezone = resolveDeviceTimezone(),
  model = getForecastModel('best_match'),
  fallbackModel = null,
  suffixed = false,
} = {}) {
  if (!spot || !spot.id || !Number.isFinite(spot.latitude) || !Number.isFinite(spot.longitude)) {
    fail('spot is invalid')
  }
  if (!Number.isFinite(retrievedAt) || retrievedAt <= 0) fail('retrieval time is invalid')
  if (!validTimezone(deviceTimezone)) fail('device timezone is invalid')
  dateParts(firstDate)
  if (!model || !getForecastModel(model.id)) fail('model is invalid')
  const {
    hourly, units, count, fields, fallbackFields,
  } = validateCoreResponse(response, model, suffixed, spot.timezone, fallbackModel)

  const availability = optionalAvailability(hourly, units, count, fields)
  const fallbackAvailability = fallbackFields
    ? optionalAvailability(hourly, units, count, fallbackFields)
    : null
  const sampleByLocalTime = new Map()
  let previousTime = ''
  let primarySampleCount = 0

  for (let index = 0; index < count; index += 1) {
    const localTime = hourly.time[index]
    const match = /^(\d{4}-\d{2}-\d{2})T(\d{2}):00$/.exec(localTime)
    if (!match || (previousTime && previousTime >= localTime)) fail('hourly times are invalid or not ascending')
    previousTime = localTime
    const hour = Number(match[2])
    if (!REQUIRED_HOURS.includes(hour) || match[1] < firstDate) continue
    let sampleFields = fields
    let sampleAvailability = availability
    let [wind, gust, sourceDirection] = [
      hourly[fields.wind][index], hourly[fields.gust][index], hourly[fields.direction][index],
    ]
    if ([wind, gust, sourceDirection].every(Number.isFinite)) {
      primarySampleCount += 1
    } else if (fallbackFields) {
      sampleFields = fallbackFields
      sampleAvailability = fallbackAvailability
      ;[wind, gust, sourceDirection] = [
        hourly[fallbackFields.wind][index],
        hourly[fallbackFields.gust][index],
        hourly[fallbackFields.direction][index],
      ]
    }
    if (![wind, gust, sourceDirection].every(Number.isFinite) || wind < 0 || gust < 0 ||
        wind > 32767 || gust > 32767) fail(`invalid wind value at ${localTime}`)
    const temperature = sampleAvailability.temperature ? hourly[sampleFields.temperature][index] : null
    sampleByLocalTime.set(localTime, {
      time: match[2],
      sustainedKt: Math.round(wind),
      gustKt: Math.round(gust),
      destinationDegrees: Math.round(((sourceDirection + 180) % 360 + 360) % 360) % 360,
      available: true,
      weather: sampleAvailability.weather
        ? weatherState(
          hourly[sampleFields.cloud][index],
          hourly[sampleFields.precipitation][index],
          hourly[sampleFields.isDay][index],
        )
        : 0,
      temperatureTenthsC: Number.isFinite(temperature) && temperature >= -3276.8 && temperature <= 3276.7
        ? roundSymmetric(temperature * 10)
        : 0,
      temperatureAvailable: Number.isFinite(temperature) &&
        temperature >= -3276.8 && temperature <= 3276.7,
    })
  }

  if (fallbackFields && primarySampleCount === 0) fail(`${model.label} is unavailable at this location`)

  const days = Array.from({ length: 5 }, (_, dayIndex) => {
    const localDate = addDays(firstDate, dayIndex)
    const { value, month, day } = dateParts(localDate)
    const samples = REQUIRED_HOURS.map((hour) => sampleByLocalTime.get(
      `${localDate}T${String(hour).padStart(2, '0')}:00`,
    ))
    if (samples.some((sample) => !sample)) fail(`target hours are incomplete for ${localDate}`)
    return {
      localDate,
      day: dayIndex === 0 ? 'TODAY' : DAY_NAMES[value.getUTCDay()],
      date: `${String(day).padStart(2, '0')} ${MONTH_NAMES[month - 1]}`,
      samples,
    }
  })

  return {
    schemaVersion: 3,
    spotId: spot.id,
    spotName: spot.displayName,
    timezone: spot.timezone,
    deviceTimezone,
    provider: 'OPEN-METEO',
    modelId: model.id,
    model: model.screenLabel,
    updatedTime: updatedTimeAt(retrievedAt, deviceTimezone),
    retrievedAt,
    days,
  }
}

export function normalizeForecastModels(response, spot, {
  models = FORECAST_MODELS,
  deviceTimezone = resolveDeviceTimezone(),
  ...options
} = {}) {
  const forecasts = {}
  const fallbackModel = models.find((model) => model.id === 'best_match') ?? null
  for (const model of models) {
    try {
      forecasts[model.id] = normalizeForecast(response, spot, {
        ...options,
        deviceTimezone,
        model,
        fallbackModel: model.id === fallbackModel?.id ? null : fallbackModel,
        suffixed: true,
      })
    } catch (error) {
      if (model.availability !== 'regional') throw error
    }
  }
  return forecasts
}

export function isNormalizedForecast(value) {
  const model = getForecastModel(value?.modelId)
  if (!value || value.schemaVersion !== 3 || typeof value.spotId !== 'string' || !value.spotId ||
      typeof value.spotName !== 'string' || !value.spotName ||
      !validTimezone(value.timezone) || !validTimezone(value.deviceTimezone) ||
      value.provider !== 'OPEN-METEO' ||
      !model || value.model !== model.screenLabel ||
      typeof value.updatedTime !== 'string' ||
      !Number.isFinite(value.retrievedAt) || value.retrievedAt <= 0 ||
      !Array.isArray(value.days) || value.days.length !== 5) return false
  if (!textFitsRenderer(value.spotName, RENDERER_TEXT_CAPACITIES.spotName) ||
      !textFitsRenderer(value.model, RENDERER_TEXT_CAPACITIES.provider) ||
      !textFitsRenderer(value.updatedTime, RENDERER_TEXT_CAPACITIES.updatedTime)) return false

  return value.days.every((day, dayIndex) => {
    if (!day || typeof day.localDate !== 'string' || typeof day.day !== 'string' ||
        typeof day.date !== 'string' || !textFitsRenderer(day.day, RENDERER_TEXT_CAPACITIES.day) ||
        !textFitsRenderer(day.date, RENDERER_TEXT_CAPACITIES.date) ||
        !Array.isArray(day.samples) || day.samples.length !== 5) return false
    try {
      if (dayIndex > 0 && day.localDate !== addDays(value.days[dayIndex - 1].localDate, 1)) return false
    } catch {
      return false
    }
    return day.samples.every((sample, sampleIndex) => sample &&
      sample.time === String(REQUIRED_HOURS[sampleIndex]).padStart(2, '0') &&
      textFitsRenderer(sample.time, RENDERER_TEXT_CAPACITIES.time) &&
      Number.isInteger(sample.sustainedKt) && sample.sustainedKt >= 0 && sample.sustainedKt <= 32767 &&
      Number.isInteger(sample.gustKt) && sample.gustKt >= 0 && sample.gustKt <= 32767 &&
      Number.isInteger(sample.destinationDegrees) && sample.destinationDegrees >= 0 && sample.destinationDegrees < 360 &&
      sample.available === true && Number.isInteger(sample.weather) && sample.weather >= 0 && sample.weather <= 8 &&
      typeof sample.temperatureAvailable === 'boolean' && Number.isInteger(sample.temperatureTenthsC) &&
      sample.temperatureTenthsC >= -32768 && sample.temperatureTenthsC <= 32767)
  })
}

export { REQUIRED_HOURS }
