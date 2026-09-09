import { describe, expect, it, vi } from 'vitest'
import { FORECAST_MODELS } from '../src/forecast/models'
import { forecastResponseForLatitude, tideTimes } from './e2e/helpers/forecast'
import { normalizeTide, isNormalizedTide } from '../src/forecast/normalizeTide'

describe('e2e forecast fixture', () => {
  it.each([
    ['Africa/Cairo', '2026-04-24T12:00:00Z'],
    ['Africa/Cairo', '2026-10-30T12:00:00Z'],
    ['Australia/Lord_Howe', '2026-10-02T12:00:00Z'],
  ])('covers exactly five dates across unusual DST transitions in %s on %s', (timezone, date) => {
    vi.useFakeTimers()
    try {
      vi.setSystemTime(new Date(date))
      const time = tideTimes(timezone)
      const tide = normalizeTide({ timezone, hourly_units: { time: 'unixtime', sea_level_height_msl: 'm' },
        hourly: { time, sea_level_height_msl: time.map((_, i) => Math.sin(i / 6) * 0.8) },
      }, { id: 'test', latitude: 0, longitude: 0, timezone })
      expect(new Set(tide.samples.map((sample) => sample.localDate)).size).toBe(5)
      const format = new Intl.DateTimeFormat('en-CA', { timeZone: timezone, year: 'numeric', month: '2-digit', day: '2-digit' })
      expect(format.format((time.at(-1) + 3600) * 1000)).not.toBe(format.format(time.at(-1) * 1000))
    } finally {
      vi.useRealTimers()
    }
  })
  it.each(['Europe/Amsterdam', 'America/Los_Angeles', 'America/Santiago', 'Asia/Makassar', 'Asia/Kolkata'])('produces valid five-day tide data in %s including DST windows', (timezone) => {
    vi.useFakeTimers()
    try {
      for (const date of ['2026-09-09T12:00:00Z', '2026-10-24T12:00:00Z', '2026-03-28T12:00:00Z']) {
        vi.setSystemTime(new Date(date))
        const times = tideTimes(timezone)
        const normalized = normalizeTide({ timezone, hourly_units: { time: 'unixtime', sea_level_height_msl: 'm' },
          hourly: { time: times, sea_level_height_msl: times.map((_, i) => Math.sin(i / 6) * 0.8) },
        }, { id: 'test', latitude: 0, longitude: 0, timezone })
        expect(isNormalizedTide(normalized)).toBe(true)
        expect(normalized.samples[0].localTime).toBe('00:00')
      }
    } finally {
      vi.useRealTimers()
    }
  })
  it('starts the local forecast on the requested timezone day', () => {
    vi.useFakeTimers()
    try {
      vi.setSystemTime(new Date('2026-09-09T23:30:00Z'))
      expect(forecastResponseForLatitude(34, 'America/Los_Angeles').hourly.time[0]).toBe('2026-09-09T08:00')
      expect(forecastResponseForLatitude(-8, 'Asia/Makassar').hourly.time[0]).toBe('2026-09-10T08:00')
    } finally {
      vi.useRealTimers()
    }
  })
  it('keeps cloud cover valid for every forecast model', () => {
    const response = forecastResponseForLatitude(52.5)

    for (const model of FORECAST_MODELS) {
      const cloudCover = response.hourly[`cloud_cover_${model.apiId}`]
      expect(cloudCover.every((value) => value >= 0 && value <= 100)).toBe(true)
    }
  })
})
