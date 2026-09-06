import { describe, expect, it } from 'vitest'

import { findNearestSpot, normalizeCoordinates } from '../src/spots/nearestSpot.js'

const spots = [
  { id: 'west', latitude: 0, longitude: 179 },
  { id: 'east', latitude: 0, longitude: -179 },
  { id: 'north', latitude: 50, longitude: 5 },
]

describe('findNearestSpot', () => {
  it('returns a spot at the exact catalog coordinate', () => {
    expect(findNearestSpot({ latitude: 50, longitude: 5 }, spots)).toBe(spots[2])
  })

  it('uses great-circle distance across the antimeridian', () => {
    expect(findNearestSpot({ latitude: 0, longitude: -179.25 }, spots)).toBe(spots[1])
  })

  it('keeps catalog order when candidates are equally far away', () => {
    const tiedSpots = [
      { id: 'first', latitude: 0, longitude: -1 },
      { id: 'second', latitude: 0, longitude: 1 },
    ]

    expect(findNearestSpot({ latitude: 0, longitude: 0 }, tiedSpots)).toBe(tiedSpots[0])
  })

  it.each([
    [{ latitude: Number.NaN, longitude: 0 }],
    [{ latitude: Number.POSITIVE_INFINITY, longitude: 0 }],
    [{ latitude: 91, longitude: 0 }],
    [{ latitude: 0, longitude: -181 }],
    [{ latitude: '50', longitude: 5 }],
    [null],
  ])('returns no spot for invalid coordinates: %j', (coordinates) => {
    expect(findNearestSpot(coordinates, spots)).toBeNull()
  })

  it('ignores catalog entries with unusable coordinates', () => {
    expect(findNearestSpot(
      { latitude: 50, longitude: 5 },
      [{ id: 'invalid', latitude: null, longitude: 5 }, spots[2]],
    )).toBe(spots[2])
  })
})

describe('normalizeCoordinates', () => {
  it('returns a fresh numeric coordinate pair', () => {
    const input = { latitude: 12.5, longitude: -45.25 }

    expect(normalizeCoordinates(input)).toEqual(input)
    expect(normalizeCoordinates(input)).not.toBe(input)
  })

  it('rejects missing, non-numeric, non-finite and out-of-range values', () => {
    expect(normalizeCoordinates({ latitude: 12.5 })).toBeNull()
    expect(normalizeCoordinates({ latitude: '12.5', longitude: -45.25 })).toBeNull()
    expect(normalizeCoordinates({ latitude: 12.5, longitude: Number.NaN })).toBeNull()
    expect(normalizeCoordinates({ latitude: -90.01, longitude: 0 })).toBeNull()
    expect(normalizeCoordinates({ latitude: 0, longitude: 180.01 })).toBeNull()
  })
})
