import { MODULE_IDS } from '../config/modules'
import { swellDashboardForecast } from '../forecast/openMeteoSwell'
import { DISPLAY_MODES, RENDERER_CONTRACT_VERSION } from './contract'

function formatSampleTime(time, timeFormat) {
  if (timeFormat !== '12-hour') return time
  const hour = Number.parseInt(time, 10)
  if (!Number.isFinite(hour)) return time
  const hour12 = hour % 12 === 0 ? 12 : hour % 12
  return `${hour12}${hour < 12 ? 'AM' : 'PM'}`
}

function rendererSample(sample, timeFormat) {
  return {
    time: formatSampleTime(sample.time, timeFormat),
    sustainedKt: sample.sustainedKt,
    gustKt: sample.gustKt,
    destinationDegrees: sample.destinationDegrees,
    available: sample.available,
    weather: sample.weather,
    temperatureTenthsC: sample.temperatureTenthsC ?? 0,
    temperatureAvailable: sample.temperatureAvailable ?? false,
  }
}

function rendererTideSamples(tide, days) {
  if (tide?.capability !== 'available') return []
  const dayIndexes = new Map(days.map((day, index) => [day.localDate, index]))
  return tide.samples.flatMap((sample) => {
    const dayIndex = dayIndexes.get(sample.localDate)
    if (dayIndex === undefined) return []
    return [{
      dayIndex,
      localHour: Number(sample.localTime.slice(0, 2)),
      seaLevelMm: sample.seaLevelMm,
      available: true,
    }]
  })
}

function rendererTideExtrema(tide, days) {
  if (tide?.capability !== 'available' || !Array.isArray(tide.extrema)) return []
  const dayIndexes = new Map(days.map((day, index) => [day.localDate, index]))
  return tide.extrema.flatMap((extremum) => {
    const dayIndex = dayIndexes.get(extremum.localDate)
    if (dayIndex === undefined) return []
    const [localHour, localMinute] = extremum.localTime.split(':').map(Number)
    return [{
      dayIndex,
      localHour,
      localMinute,
      seaLevelMm: extremum.seaLevelMm,
      isHigh: extremum.type === 'high',
      available: true,
    }]
  })
}

function formatUpdatedTime(forecast, timeFormat) {
  if (!Number.isFinite(forecast.retrievedAt) || !forecast.deviceTimezone) return forecast.updatedTime
  const twelveHour = timeFormat === '12-hour'
  const parts = new Intl.DateTimeFormat(twelveHour ? 'en-US' : 'en-GB', {
    timeZone: forecast.deviceTimezone,
    day: '2-digit',
    month: 'short',
    hour: twelveHour ? 'numeric' : '2-digit',
    minute: '2-digit',
    hourCycle: twelveHour ? 'h12' : 'h23',
  }).formatToParts(new Date(forecast.retrievedAt))
  const values = Object.fromEntries(parts.map((part) => [part.type, part.value]))
  const date = `${values.day} ${values.month.toUpperCase()}`
  return twelveHour
    ? `${date} ${values.hour}${values.dayPeriod.toUpperCase()}`
    : `${date} ${values.hour}:${values.minute}`
}

export function createRendererInput(forecast, config) {
  const sizes = { off: 0, small: 1, large: 2 }
  const windSize = sizes[config.windSize] ?? (config.swellFocus ? 1 : 2)
  const swellSize = sizes[config.swellSize] ?? (config.swellFocus ? 2 : 0)
  if (swellSize) forecast = swellDashboardForecast(forecast, config.swell, windSize > 0 || config.showWeather || config.showTemperature)
  const swell = config.swell?.spotId === forecast.spotId ? config.swell : null
  const swellSamples = new Map((swell?.samples ?? []).map((sample) => [`${sample.localDate}T${sample.time}`, sample]))
  const displayMode = config.showThreshold && windSize === 2
    ? DISPLAY_MODES['threshold-line']
    : DISPLAY_MODES.solid

  const tide = config.tide ?? forecast.tide
  const tideSamples = rendererTideSamples(tide, forecast.days)
  const tideExtrema = rendererTideExtrema(tide, forecast.days)
  return {
    version: RENDERER_CONTRACT_VERSION,
    swellFocus: swellSize > 0,
    windSize,
    swellSize,
    moduleOrder: config.moduleOrder?.map(id => MODULE_IDS.indexOf(id)),
    swellHourly: (swell?.hourly ?? []).flatMap((sample) => {
      const dayIndex = forecast.days.findIndex((day) => day.localDate === sample.localDate)
      return dayIndex < 0 ? [] : [{ dayIndex, hour: Number(sample.time), heightCm: sample.heightCm, secondaryHeightCm: sample.secondaryHeightCm ?? -1 }]
    }),
    spotName: forecast.spotName,
    provider: forecast.model ?? forecast.provider ?? 'OPEN-METEO',
    updatedTime: formatUpdatedTime(forecast, config.timeFormat),
    state: forecast.state ?? 0,
    refreshFailed: Boolean(forecast.refreshFailed || (swellSize && config.swellStatus === 'failed')),
    ageHours: forecast.ageHours ?? 0,
    batteryPercent: forecast.batteryPercent ?? 70,
    displayMode,
    thresholdKt: config.threshold,
    showWeather: config.showWeather ?? true,
    showTemperature: config.showTemperature ?? false,
    showTide: config.showTide ?? false,
    showDedicatedFooter: config.showDedicatedFooter ?? false,
    use24Hour: config.timeFormat !== '12-hour',
    temperatureFahrenheit: config.temperatureUnit === 'fahrenheit',
    tideAvailable: tide?.capability === 'available' && tideSamples.length >= 2,
    tideSamples,
    tideExtrema,
    days: forecast.days.map((day) => ({
      day: day.day,
      date: day.date,
      samples: day.samples.map((sample) => ({
        ...rendererSample(sample, config.timeFormat),
        secondarySwellHeightCm: swellSamples.get(`${day.localDate}T${sample.time}`)?.secondaryHeightCm ?? -1,
        secondarySwellPeriodTenths: swellSamples.get(`${day.localDate}T${sample.time}`)?.secondaryPeriodTenths ?? -1,
        secondarySwellDestinationDegrees: swellSamples.get(`${day.localDate}T${sample.time}`)?.secondaryDestinationDegrees ?? -1,
        swellHeightCm: swellSamples.get(`${day.localDate}T${sample.time}`)?.heightCm ?? -1,
        swellPeriodTenths: swellSamples.get(`${day.localDate}T${sample.time}`)?.periodTenths ?? -1,
        swellDestinationDegrees: swellSamples.get(`${day.localDate}T${sample.time}`)?.destinationDegrees ?? -1,
      })),
    })),
  }
}
