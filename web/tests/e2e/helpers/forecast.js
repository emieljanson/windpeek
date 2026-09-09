import { FORECAST_MODELS } from '../../../src/forecast/models'

export function amsterdamDate(offset = 0, timezone = 'Europe/Amsterdam') {
  const parts = new Intl.DateTimeFormat('en-CA', {
    timeZone: timezone, year: 'numeric', month: '2-digit', day: '2-digit',
  }).formatToParts(new Date())
  const values = Object.fromEntries(parts.map((part) => [part.type, part.value]))
  const date = new Date(Date.UTC(Number(values.year), Number(values.month) - 1, Number(values.day) + offset))
  return date.toISOString().slice(0, 10)
}

export function forecastResponseForLatitude(latitude, timezone = 'Europe/Amsterdam') {
  const times = Array.from({ length: 5 }, (_, day) => [8, 11, 14, 17, 20]
    .map((hour) => `${amsterdamDate(day, timezone)}T${String(hour).padStart(2, '0')}:00`)).flat()
  const offset = latitude > 52 ? 4 : 0
  const hourlyUnits = { time: 'iso8601' }
  const hourly = { time: times }

  FORECAST_MODELS.forEach((model, modelIndex) => {
    const id = model.apiId
    Object.assign(hourlyUnits, {
      [`wind_speed_10m_${id}`]: 'kn',
      [`wind_gusts_10m_${id}`]: 'kn',
      [`wind_direction_10m_${id}`]: '°',
      [`cloud_cover_${id}`]: '%',
      [`precipitation_${id}`]: 'mm',
      [`is_day_${id}`]: '',
      [`temperature_2m_${id}`]: '°C',
    })
    Object.assign(hourly, {
      [`wind_speed_10m_${id}`]: times.map((_, index) => 11 + offset + modelIndex * 3 + (index % 5)),
      [`wind_gusts_10m_${id}`]: times.map((_, index) => 17 + offset + modelIndex * 3 + (index % 5)),
      [`wind_direction_10m_${id}`]: times.map(() => 90 + modelIndex * 15),
      [`cloud_cover_${id}`]: times.map(() => Math.min(20 + modelIndex * 10, 100)),
      [`precipitation_${id}`]: times.map(() => 0),
      [`is_day_${id}`]: times.map(() => 1),
      [`temperature_2m_${id}`]: times.map((_, index) => 12 + modelIndex + (index % 5)),
    })
  })

  return { timezone, hourly_units: hourlyUnits, hourly }
}
