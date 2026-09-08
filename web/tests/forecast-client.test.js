import { describe, expect, it, vi } from 'vitest'
import { buildForecastUrl, fetchOpenMeteoForecasts } from '../src/forecast/openMeteo'
import {
  ALWAYS_FORECAST_MODEL_IDS,
  forecastModelsForSpot,
  getForecastModel,
} from '../src/forecast/models'
import { SPOTS } from '../src/spots'

function minimalResponse(models = forecastModelsForSpot(SPOTS[1])) {
  const times = []
  for (let day = 26; day <= 30; day += 1) {
    for (const hour of [8, 11, 14, 17, 20]) times.push(`2026-08-${day}T${String(hour).padStart(2, '0')}:00`)
  }
  const hourlyUnits = { time: 'iso8601' }
  const hourly = { time: times }
  models.forEach((model, modelIndex) => {
    const modelId = model.apiId
    Object.assign(hourlyUnits, {
      [`wind_speed_10m_${modelId}`]: 'kn',
      [`wind_gusts_10m_${modelId}`]: 'kn',
      [`wind_direction_10m_${modelId}`]: '°',
      [`cloud_cover_${modelId}`]: '%',
      [`precipitation_${modelId}`]: 'mm',
      [`is_day_${modelId}`]: '',
      [`temperature_2m_${modelId}`]: '°C',
    })
    Object.assign(hourly, {
      [`wind_speed_10m_${modelId}`]: times.map((_, index) => 10 + modelIndex * 4 + index / 10),
      [`wind_gusts_10m_${modelId}`]: times.map((_, index) => 15 + modelIndex * 4 + index / 10),
      [`wind_direction_10m_${modelId}`]: times.map(() => 90),
      [`cloud_cover_${modelId}`]: times.map(() => 10),
      [`precipitation_${modelId}`]: times.map(() => 0),
      [`is_day_${modelId}`]: times.map(() => 1),
      [`temperature_2m_${modelId}`]: times.map((_, index) => 8 + modelIndex + index / 10),
    })
  })
  return {
    timezone: 'Europe/Amsterdam',
    hourly_units: hourlyUnits,
    hourly,
  }
}

describe('Open-Meteo forecast client', () => {
  it('always offers worldwide models and adds regional candidates per location', () => {
    expect(ALWAYS_FORECAST_MODEL_IDS).toEqual([
      'best_match', 'ecmwf_ifs', 'icon_seamless', 'ncep_gfs_seamless',
    ])
    expect(forecastModelsForSpot({
      countryCode: 'nl', latitude: 52.5, longitude: 5,
    }).map((model) => model.id)).toEqual([
      ...ALWAYS_FORECAST_MODEL_IDS, 'knmi_harmonie', 'dmi_harmonie',
    ])
    expect(forecastModelsForSpot({
      countryCode: 'jp', latitude: 35.7, longitude: 139.7,
    }).map((model) => model.id)).toEqual([...ALWAYS_FORECAST_MODEL_IDS, 'jma_msm'])
  })

  it('limits HRRR to the contiguous United States', () => {
    expect(forecastModelsForSpot({
      countryCode: 'us', latitude: 37.8, longitude: -122.4,
    }).map((model) => model.id)).toContain('noaa_hrrr')
    expect(forecastModelsForSpot({
      countryCode: 'us', latitude: 21.3, longitude: -157.8,
    }).map((model) => model.id)).not.toContain('noaa_hrrr')
  })

  it('uses stable internal IDs and the exact selector labels', () => {
    expect(getForecastModel('knmi_harmonie')).toMatchObject({
      apiId: 'knmi_harmonie_arome_netherlands',
      label: 'KNMI HARMONIE',
      screenLabel: 'KNMI HARMONIE',
    })
    expect(getForecastModel('ecmwf_ifs')).toMatchObject({
      label: 'ECMWF IFS', screenLabel: 'ECMWF IFS',
    })
  })

  it('requests Best Match plus every curated comparison model in one call', () => {
    const url = new URL(buildForecastUrl(SPOTS[1]))
    expect(url.origin + url.pathname).toBe('https://api.open-meteo.com/v1/forecast')
    expect(url.searchParams.get('latitude')).toBe('51.750600')
    expect(url.searchParams.get('longitude')).toBe('3.857700')
    expect(url.searchParams.get('models')).toBe(
      forecastModelsForSpot(SPOTS[1]).map((model) => model.apiId).join(','),
    )
    expect(url.searchParams.get('wind_speed_unit')).toBe('kn')
    expect(url.searchParams.get('timezone')).toBe('Europe/Amsterdam')
    expect(url.searchParams.get('forecast_days')).toBe('5')
    expect(url.searchParams.get('hourly')?.split(',')).toEqual([
      'wind_speed_10m', 'wind_gusts_10m', 'wind_direction_10m',
      'cloud_cover', 'precipitation', 'is_day',
      'temperature_2m',
    ])
  })

  it('uses the confirmed spot timezone instead of assuming Amsterdam', () => {
    const url = new URL(buildForecastUrl({
      ...SPOTS[1],
      timezone: 'Europe/Lisbon',
    }))
    expect(url.searchParams.get('timezone')).toBe('Europe/Lisbon')
  })

  it('fetches and normalizes a successful response without a Windpeek backend', async () => {
    const fetchImpl = vi.fn().mockResolvedValue({ ok: true, json: async () => minimalResponse() })
    const forecasts = await fetchOpenMeteoForecasts(SPOTS[1], {
      fetchImpl,
      now: () => Date.parse('2026-08-26T12:00:00Z'),
      firstDate: '2026-08-26',
    })
    expect(fetchImpl).toHaveBeenCalledOnce()
    expect(Object.keys(forecasts)).toEqual(
      forecastModelsForSpot(SPOTS[1]).map((model) => model.id),
    )
    expect(forecasts.best_match).toMatchObject({
      spotId: 'brouwersdam', modelId: 'best_match', model: 'BEST MATCH',
    })
    expect(forecasts.ncep_gfs_seamless.model)
      .toBe(getForecastModel('ncep_gfs_seamless').screenLabel)
    expect(forecasts.ncep_gfs_seamless.days[0].samples[0].sustainedKt)
      .toBeGreaterThan(forecasts.best_match.days[0].samples[0].sustainedKt)
  })

  it('rejects a non-success response and clears its timeout', async () => {
    vi.useFakeTimers()
    const fetchImpl = vi.fn().mockResolvedValue({ ok: false, status: 503 })
    await expect(fetchOpenMeteoForecasts(SPOTS[1], { fetchImpl, timeoutMs: 25 }))
      .rejects.toThrow('HTTP 503')
    expect(vi.getTimerCount()).toBe(0)
    vi.useRealTimers()
  })

  it('aborts a request that exceeds the browser timeout', async () => {
    vi.useFakeTimers()
    const fetchImpl = vi.fn((_url, { signal }) => new Promise((_resolve, reject) => {
      signal.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')))
    }))
    const pending = fetchOpenMeteoForecasts(SPOTS[1], { fetchImpl, timeoutMs: 25 })
    const result = expect(pending).rejects.toThrow('timed out')
    await vi.advanceTimersByTimeAsync(25)
    await result
    vi.useRealTimers()
  })
})
