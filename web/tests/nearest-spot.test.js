import { describe, expect, it } from 'vitest'

import {
  findNearbyDefaultSpot,
  findNearestSpot,
  normalizeCoordinates,
} from '../src/spots/nearestSpot.js'
import nearbyIndex from '../src/spots/nearby.generated.json'

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

describe('findNearbyDefaultSpot', () => {
  const nearbySpots = [
    { id: 'club', latitude: 52.1, longitude: 5.1 },
    { id: 'local-surf-spot', latitude: 52.2, longitude: 5.2 },
    { id: 'popular-surf-spot', latitude: 52.5, longitude: 5.5 },
    { id: 'far-away-icon', latitude: 54, longitude: 7 },
  ]
  const recommendations = [
    { id: 'local-surf-spot', priority: 1 },
    { id: 'popular-surf-spot', priority: 3 },
    { id: 'far-away-icon', priority: 4 },
  ]

  it('chooses the nearest recommended spot even when another has higher priority', () => {
    expect(findNearbyDefaultSpot(
      { latitude: 52.1, longitude: 5.1 },
      nearbySpots,
      { recommendations },
    )).toBe(nearbySpots[1])
  })

  it('uses distance when surf spots have the same popularity', () => {
    const equalRecommendations = recommendations.map((spot) => ({ ...spot, priority: 1 }))

    expect(findNearbyDefaultSpot(
      { latitude: 52.1, longitude: 5.1 },
      nearbySpots,
      { recommendations: equalRecommendations },
    )).toBe(nearbySpots[1])
  })

  it('chooses the nearest recommended spot regardless of distance', () => {
    expect(findNearbyDefaultSpot(
      { latitude: 0, longitude: 0 },
      nearbySpots,
      { recommendations },
    )).toBe(nearbySpots[1])
  })

  it.each([
    ['Bali', -8.65, 115.22],
    ['Honolulu', 21.31, -157.86],
  ])('still selects a recommended spot when %s has no local catalog coverage', (_, latitude, longitude) => {
    expect(nearbyIndex.some(({ id }) => id === findNearbyDefaultSpot({ latitude, longitude })?.id)).toBe(true)
  })

  it('chooses the nearest curated spot for a broad Utrecht-region IP location', () => {
    expect(findNearbyDefaultSpot({ latitude: 52.09083, longitude: 5.12222 })?.name)
      .toBe('Edam')
  })

  it('chooses Third Avenue for San Francisco', () => {
    expect(findNearbyDefaultSpot({ latitude: 37.7749, longitude: -122.4194 })?.name)
      .toBe('Third Avenue')
    expect(nearbyIndex.some(({ id }) => id === 'spot-2h4kyt')).toBe(false)
  })

  it('includes the popular Dutch surf spots but excludes the nearby sailing club', () => {
    const priorities = new Map(nearbyIndex.map(({ id, priority }) => [id, priority]))

    expect(priorities.get('spot-yd8j5z')).toBe(3)
    expect(priorities.get('spot-1ljalze')).toBe(3)
    expect(priorities.has('spot-tecvwf')).toBe(false)
    expect(priorities.get('edam')).toBe(1)
    expect(priorities.get('castricum-aan-zee')).toBe(1)
    expect(priorities.has('spot-xjkdwp')).toBe(false)
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
