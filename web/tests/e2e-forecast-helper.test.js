import { describe, expect, it, vi } from 'vitest'
import { FORECAST_MODELS } from '../src/forecast/models'
import { forecastResponseForLatitude } from './e2e/helpers/forecast'

describe('e2e forecast fixture', () => {
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
