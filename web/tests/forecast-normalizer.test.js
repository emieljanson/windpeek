import { describe, expect, it } from 'vitest'
import { normalizeForecast, normalizeForecastModels } from '../src/forecast/normalizeForecast'
import { getForecastModel } from '../src/forecast/models'
import { SPOTS } from '../src/spots'

function responseFor({ hours = [8, 11, 14, 17, 20], speedUnit = 'kn', omit = '' } = {}) {
  const times = []
  for (let day = 26; day <= 30; day += 1) {
    for (const hour of hours) times.push(`2026-08-${day}T${String(hour).padStart(2, '0')}:00`)
  }
  const hourly = {
    time: times,
    wind_speed_10m: times.map((_, index) => 10.4 + index),
    wind_gusts_10m: times.map((_, index) => 16.6 + index),
    wind_direction_10m: times.map((_, index) => (index === 0 ? 190 : 90)),
    cloud_cover: times.map((_, index) => index === 0 ? 21 : 70),
    precipitation: times.map((_, index) => index === 1 ? 1 : 0),
    is_day: times.map(() => 1),
    temperature_2m: times.map((_, index) => index === 0 ? -2.35 : 12.04 + index),
  }
  delete hourly[omit]
  return {
    timezone: 'Europe/Amsterdam',
    hourly_units: {
      wind_speed_10m: speedUnit,
      wind_gusts_10m: 'kn',
      wind_direction_10m: '°',
      cloud_cover: '%',
      precipitation: 'mm',
      temperature_2m: '°C',
    },
    hourly,
  }
}

describe('forecast normalizer', () => {
  it('uses device-local update time while keeping forecast hours local to the spot', () => {
    const forecast = normalizeForecast(responseFor(), SPOTS[1], {
      firstDate: '2026-08-26',
      retrievedAt: Date.parse('2026-08-26T12:05:00Z'),
      deviceTimezone: 'America/New_York',
    })
    expect(forecast).toMatchObject({
      schemaVersion: 3,
      spotId: 'brouwersdam',
      spotName: 'BROUWERSDAM',
      timezone: 'Europe/Amsterdam',
      deviceTimezone: 'America/New_York',
      provider: 'OPEN-METEO',
      model: 'BEST MATCH',
      updatedTime: '26 AUG 8AM',
    })
    expect(forecast.days.map((day) => day.day)).toEqual(['TODAY', 'THURSDAY', 'FRIDAY', 'SATURDAY', 'SUNDAY'])
    expect(forecast.days[0].date).toBe('26 AUG')
    expect(forecast.days[0].samples.map((sample) => sample.time)).toEqual(['08', '11', '14', '17', '20'])
    expect(forecast.days[0].samples[0]).toMatchObject({
      sustainedKt: 10, gustKt: 17, destinationDegrees: 10, weather: 3, available: true,
      temperatureTenthsC: -24, temperatureAvailable: true,
    })
    expect(forecast.days[0].samples[1].weather).toBe(7)
  })

  it.each([
    ['missing core hourly arrays', { omit: 'wind_gusts_10m' }],
    ['an incompatible speed unit', { speedUnit: 'km/h' }],
    ['an incomplete target-hour sequence', { hours: [8, 11, 14, 20] }],
  ])('rejects %s', (_name, options) => {
    expect(() => normalizeForecast(responseFor(options), SPOTS[1], {
      firstDate: '2026-08-26', retrievedAt: 1_777_000_000_000,
    })).toThrow()
  })

  it('keeps weather unavailable when optional weather arrays are absent', () => {
    const forecast = normalizeForecast(responseFor({ omit: 'cloud_cover' }), SPOTS[1], {
      firstDate: '2026-08-26', retrievedAt: 1_777_000_000_000,
    })
    expect(forecast.days.flatMap((day) => day.samples).every((sample) => sample.weather === 0)).toBe(true)
  })

  it('keeps temperature independently unavailable when a value or the full array is missing', () => {
    const partial = responseFor()
    partial.hourly.temperature_2m[2] = null
    const forecast = normalizeForecast(partial, SPOTS[1], {
      firstDate: '2026-08-26', retrievedAt: 1_777_000_000_000,
    })
    expect(forecast.days[0].samples[1].temperatureAvailable).toBe(true)
    expect(forecast.days[0].samples[2]).toMatchObject({
      temperatureTenthsC: 0, temperatureAvailable: false,
    })

    const absent = normalizeForecast(responseFor({ omit: 'temperature_2m' }), SPOTS[1], {
      firstDate: '2026-08-26', retrievedAt: 1_777_000_000_000,
    })
    expect(absent.days.flatMap((day) => day.samples)
      .every((sample) => sample.temperatureAvailable === false)).toBe(true)
  })

  it('rounds cloud cover before applying the firmware weather boundaries', () => {
    const response = responseFor()
    response.hourly.cloud_cover[0] = 20.4
    response.hourly.cloud_cover[2] = 60.4
    const forecast = normalizeForecast(response, SPOTS[1], {
      firstDate: '2026-08-26', retrievedAt: 1_777_000_000_000,
    })

    expect(forecast.days[0].samples[0].weather).toBe(1)
    expect(forecast.days[0].samples[2].weather).toBe(3)
  })

  it('normalizes suffixed model arrays into independently selectable forecasts', () => {
    const response = responseFor()
    for (const modelId of ['best_match', 'ncep_gfs_seamless']) {
      for (const field of [
        'wind_speed_10m', 'wind_gusts_10m', 'wind_direction_10m',
        'cloud_cover', 'precipitation', 'is_day',
        'temperature_2m',
      ]) {
        response.hourly[`${field}_${modelId}`] = response.hourly[field].map((value) => (
          modelId === 'ncep_gfs_seamless' && field === 'wind_speed_10m' ? value + 8 : value
        ))
        response.hourly_units[`${field}_${modelId}`] = response.hourly_units[field] ?? ''
      }
    }

    const forecasts = normalizeForecastModels(response, SPOTS[1], {
      models: [getForecastModel('best_match'), getForecastModel('ncep_gfs_seamless')],
      firstDate: '2026-08-26',
      retrievedAt: 1_777_000_000_000,
    })

    expect(forecasts.best_match).toMatchObject({ modelId: 'best_match', model: 'BEST MATCH' })
    expect(forecasts.ncep_gfs_seamless)
      .toMatchObject({ modelId: 'ncep_gfs_seamless', model: 'NOAA GFS' })
    expect(forecasts.ncep_gfs_seamless.days[0].samples[0].sustainedKt)
      .toBe(forecasts.best_match.days[0].samples[0].sustainedKt + 8)
  })

  it('uses Best Match after a high-resolution regional model runs out', () => {
    const response = responseFor()
    const regionalId = 'knmi_harmonie'
    const regionalApiId = getForecastModel(regionalId).apiId
    for (const modelId of ['best_match', regionalApiId]) {
      for (const field of [
        'wind_speed_10m', 'wind_gusts_10m', 'wind_direction_10m',
        'cloud_cover', 'precipitation', 'is_day', 'temperature_2m',
      ]) {
        response.hourly[`${field}_${modelId}`] = response.hourly[field].map((value, index) => (
          modelId === regionalApiId && index >= 15 ? null : value + (modelId === regionalApiId ? 4 : 0)
        ))
        response.hourly_units[`${field}_${modelId}`] = response.hourly_units[field] ?? ''
      }
    }

    const forecasts = normalizeForecastModels(response, SPOTS[1], {
      models: [getForecastModel('best_match'), getForecastModel(regionalId)],
      firstDate: '2026-08-26',
      retrievedAt: 1_777_000_000_000,
    })

    expect(forecasts[regionalId].days[0].samples[0].sustainedKt)
      .toBe(forecasts.best_match.days[0].samples[0].sustainedKt + 4)
    expect(forecasts[regionalId].days[4].samples[0].sustainedKt)
      .toBe(forecasts.best_match.days[4].samples[0].sustainedKt)
  })
})
