import { describe, expect, it } from 'vitest'
import { brouwersdamTide } from '../src/fixtures/brouwersdam'

describe('Brouwersdam tide fixture', () => {
  it('places alternating extrema on matching peaks and valleys in the sample curve', () => {
    brouwersdamTide.extrema.forEach((extremum, extremumIndex) => {
      const index = brouwersdamTide.samples.findIndex((sample) => (
        sample.localDate === extremum.localDate && sample.localTime === extremum.localTime
      ))
      const sample = brouwersdamTide.samples[index]
      let plateauStart = index
      let plateauEnd = index
      while (brouwersdamTide.samples[plateauStart - 1]?.seaLevelMm === sample.seaLevelMm) plateauStart -= 1
      while (brouwersdamTide.samples[plateauEnd + 1]?.seaLevelMm === sample.seaLevelMm) plateauEnd += 1
      const previous = brouwersdamTide.samples[plateauStart - 1]
      const next = brouwersdamTide.samples[plateauEnd + 1]

      expect(index).toBeGreaterThan(0)
      expect(sample.seaLevelMm).toBe(extremum.seaLevelMm)
      expect(extremum.type === 'high'
        ? sample.seaLevelMm > previous.seaLevelMm && sample.seaLevelMm > next.seaLevelMm
        : sample.seaLevelMm < previous.seaLevelMm && sample.seaLevelMm < next.seaLevelMm).toBe(true)
      if (extremumIndex > 0) {
        expect(extremum.type).not.toBe(brouwersdamTide.extrema[extremumIndex - 1].type)
      }
    })
  })
})
