import { describe, expect, it } from 'vitest'
import { readCachedForecast, writeCachedForecasts } from '../src/forecast/forecastCache'

function memoryStorage() {
  const values = new Map()
  return {
    getItem: (key) => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
  }
}

function forecast(modelId = 'best_match', model = 'BEST MATCH') {
  return {
    schemaVersion: 2,
    spotId: 'brouwersdam',
    spotName: 'BROUWERSDAM',
    timezone: 'Europe/Amsterdam',
    provider: 'OPEN-METEO',
    modelId,
    model,
    updatedTime: '26 AUG 2PM',
    retrievedAt: 1_777_000_000_000,
    days: Array.from({ length: 5 }, (_, day) => ({
      localDate: `2026-08-${26 + day}`,
      day: day === 0 ? 'TODAY' : 'THURSDAY',
      date: `${26 + day} AUG`,
      samples: [8, 11, 14, 17, 20].map((hour) => ({
        time: String(hour).padStart(2, '0'), sustainedKt: 12, gustKt: 18,
        destinationDegrees: 270, available: true, weather: 1,
        temperatureTenthsC: 125, temperatureAvailable: true,
      })),
    })),
  }
}

describe('forecast cache', () => {
  it('round-trips independent model forecasts for the same spot', () => {
    const storage = memoryStorage()
    expect(writeCachedForecasts([forecast()], storage)).toBe(true)
    expect(writeCachedForecasts([forecast('ncep_gfs_seamless', 'NOAA GFS')], storage)).toBe(true)
    expect(readCachedForecast('brouwersdam', 'best_match', storage)).toEqual(forecast())
    expect(readCachedForecast('brouwersdam', 'ncep_gfs_seamless', storage))
      .toEqual(forecast('ncep_gfs_seamless', 'NOAA GFS'))
    expect(readCachedForecast('edam', 'best_match', storage)).toBeNull()
  })

  it('round-trips forecasts for valid personal-spot timezones', () => {
    const storage = memoryStorage()
    const lisbon = { ...forecast(), spotId: 'personal-lisbon', timezone: 'Europe/Lisbon' }
    expect(writeCachedForecasts([lisbon], storage)).toBe(true)
    expect(readCachedForecast('personal-lisbon', 'best_match', storage)).toEqual(lisbon)
  })

  it.each([
    '{not-json',
    JSON.stringify({ version: 99, spots: {} }),
    JSON.stringify({ version: 2, spots: { brouwersdam: { best_match: { schemaVersion: 1 } } } }),
  ])('rejects malformed or incompatible cache content', (value) => {
    const storage = memoryStorage()
    storage.setItem('windscout.forecasts', value)
    expect(readCachedForecast('brouwersdam', 'best_match', storage)).toBeNull()
  })

  it('writes a model batch and preserves it when a later batch contains invalid data', () => {
    const storage = memoryStorage()
    const bestMatch = forecast()
    const gfs = forecast('ncep_gfs_seamless', 'NOAA GFS')
    expect(writeCachedForecasts({ best_match: bestMatch, ncep_gfs_seamless: gfs }, storage)).toBe(true)
    const cached = storage.getItem('windscout.forecasts')
    expect(readCachedForecast('brouwersdam', 'ncep_gfs_seamless', storage)).toEqual(gfs)

    expect(writeCachedForecasts([bestMatch, { ...gfs, days: [] }], storage)).toBe(false)
    expect(storage.getItem('windscout.forecasts')).toBe(cached)
  })

  it('rejects structurally valid data that cannot cross the renderer bridge', () => {
    const storage = memoryStorage()
    const oversizedName = forecast()
    oversizedName.spotName = 'x'.repeat(96)
    expect(writeCachedForecasts([oversizedName], storage)).toBe(false)

    const oversizedWind = forecast()
    oversizedWind.days[0].samples[0].sustainedKt = 32768
    expect(writeCachedForecasts([oversizedWind], storage)).toBe(false)
  })
})
