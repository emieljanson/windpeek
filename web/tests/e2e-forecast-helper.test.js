import { describe, expect, it } from 'vitest'
import { FORECAST_MODELS } from '../src/forecast/models'
import { forecastResponseForLatitude } from './e2e/helpers/forecast'

describe('e2e forecast fixture', () => {
  it('keeps cloud cover valid for every forecast model', () => {
    const response = forecastResponseForLatitude(52.5)

    for (const model of FORECAST_MODELS) {
      const cloudCover = response.hourly[`cloud_cover_${model.apiId}`]
      expect(cloudCover.every((value) => value >= 0 && value <= 100)).toBe(true)
    }
  })
})
