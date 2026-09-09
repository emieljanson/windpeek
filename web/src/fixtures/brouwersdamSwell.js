import { brouwersdamForecast } from './brouwersdam'

// Illustrative swell for reproducible product renders, aligned with the wind fixture.
const heights = [110, 145, 180, 155, 120, 90]
const hourly = brouwersdamForecast.days.flatMap((day, index) => Array.from({ length: 24 }, (_, hour) => ({
  localDate: day.localDate,
  time: String(hour).padStart(2, '0'),
  heightCm: Math.round(heights[index] + (heights[index + 1] - heights[index]) * hour / 24),
  periodTenths: [90, 100, 110, 100, 90][index],
  destinationDegrees: 115,
  secondaryHeightCm: 35 + index * 5,
})))

export const brouwersdamSwell = {
  spotId: brouwersdamForecast.spotId,
  spotName: brouwersdamForecast.spotName,
  timezone: brouwersdamForecast.timezone,
  retrievedAt: brouwersdamForecast.retrievedAt,
  model: 'best_match',
  available: true,
  hourly,
  samples: hourly.filter(sample => ['08', '11', '14', '17', '20'].includes(sample.time)),
}
