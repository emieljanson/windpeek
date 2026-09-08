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
  const displayMode = config.showThreshold
    ? DISPLAY_MODES['threshold-line']
    : DISPLAY_MODES.solid

  const tide = config.tide ?? forecast.tide
  const tideSamples = rendererTideSamples(tide, forecast.days)
  const tideExtrema = rendererTideExtrema(tide, forecast.days)
  return {
    version: RENDERER_CONTRACT_VERSION,
    spotName: forecast.spotName,
    provider: forecast.model ?? forecast.provider ?? 'OPEN-METEO',
    updatedTime: formatUpdatedTime(forecast, config.timeFormat),
    state: forecast.state ?? 0,
    refreshFailed: forecast.refreshFailed ?? false,
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
      samples: day.samples.map((sample) => rendererSample(sample, config.timeFormat)),
    })),
  }
}

