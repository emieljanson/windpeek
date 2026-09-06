import { describe, expect, it } from 'vitest'
import { brouwersdamTide } from '../src/fixtures/brouwersdam'

describe('Brouwersdam tide fixture', () => {
  it('places each extremum on a matching peak or valley in the sample curve', () => {
    for (const extremum of brouwersdamTide.extrema) {
      const index = brouwersdamTide.samples.findIndex((sample) => (
        sample.localDate === extremum.localDate && sample.localTime === extremum.localTime
      ))
      const previous = brouwersdamTide.samples[index - 1]
      const sample = brouwersdamTide.samples[index]
      const next = brouwersdamTide.samples[index + 1]

      expect(index).toBeGreaterThan(0)
      expect(sample.seaLevelMm).toBe(extremum.seaLevelMm)
      expect(extremum.type === 'high'
        ? sample.seaLevelMm > previous.seaLevelMm && sample.seaLevelMm > next.seaLevelMm
        : sample.seaLevelMm < previous.seaLevelMm && sample.seaLevelMm < next.seaLevelMm).toBe(true)
    }
  })
})
