import { describe, expect, it } from 'vitest'
import { consolidateLocations } from '../scripts/spots/lib/consolidate-locations.mjs'
import { searchSpots } from '../src/spots/searchSpots'
import { getSpot } from '../src/spots'

const spot = (id, latitude, extra = {}) => ({ id, name: id, latitude, longitude: 0, countryCode: 'gb', timezone: 'Europe/London', ...extra })

describe('one kilometre forecast locations', () => {
  it('keeps the curated centre and makes removed names searchable', () => {
    const catalog = [spot('a', 50, { name: 'Beach North' }), spot('b', 50.005, { name: 'Town Beach' })]
    const result = consolidateLocations(catalog, { preferred: [{ id: 'b', priority: 2 }] })
    expect(result).toHaveLength(1)
    expect(result[0]).toMatchObject({ id: 'b', aliases: ['Beach North'], aliasIds: ['a'] })
    expect(searchSpots(result, 'beach north')).toEqual(result)
    expect(catalog[1].aliases).toBeUndefined()
  })

  it('does not chain a coastline into a distant centre', () => {
    const result = consolidateLocations([spot('a', 50), spot('b', 50.008), spot('c', 50.016)])
    expect(result.map((s) => s.id)).toEqual(['a', 'c'])
    expect(result[0].aliasIds).toEqual(['b'])
  })

  it('keeps timezone and known country boundaries separate', () => {
    expect(consolidateLocations([
      spot('a', 50), spot('b', 50, { timezone: 'Europe/Paris' }), spot('c', 50, { countryCode: 'fr' }),
    ])).toHaveLength(3)
  })

  it('resolves a former Third Avenue ID to its retained forecast location', () => {
    expect(getSpot('spot-2h4kyt')?.id).toBe('third-avenue')
  })

  it('retains names and IDs through an explicit wider merge and fails on stale decisions', () => {
    const input = [spot('a', 50), spot('b', 50.018)]
    const result = consolidateLocations(input, { reviewedMerges: [{ fromId: 'b', toId: 'a' }] })
    expect(result).toHaveLength(1)
    expect(result[0].aliasIds).toEqual(['b'])
    expect(() => consolidateLocations(input, { reviewedMerges: [{ fromId: 'missing', toId: 'a' }] })).toThrow('Stale')
    expect(() => consolidateLocations([spot('a', 50), spot('b', 51)], { reviewedMerges: [{ fromId: 'b', toId: 'a' }] })).toThrow('5 km')
  })

  it('only permits distant bad-pin merges with evidence and a bounded explicit radius', () => {
    const input = [spot('a', 50), spot('b', 50.14)]
    const merge = { fromId: 'b', toId: 'a', maxDistanceMeters: 17000, evidence: 'Reviewed beach coordinates' }
    expect(consolidateLocations(input)).toHaveLength(2)
    expect(consolidateLocations(input, { reviewedMerges: [merge] })[0].aliasIds).toEqual(['b'])
    expect(() => consolidateLocations(input, { reviewedMerges: [{ ...merge, evidence: '' }] })).toThrow('require evidence')
    expect(() => consolidateLocations(input, { reviewedMerges: [{ ...merge, maxDistanceMeters: 26000 }] })).toThrow('25 km')
    expect(() => consolidateLocations(input, { reviewedMerges: [{ ...merge, maxDistanceMeters: 14000 }] })).toThrow('14 km')
  })

  it('corrects a reviewed pin before clustering without mutating source records', () => {
    const input = [spot('a', 50), spot('b', 50.14)]
    const correction = { id: 'b', latitude: 50.005, longitude: 0, evidence: 'Published lifeguard station coordinates' }
    expect(consolidateLocations(input, { coordinateCorrections: [correction] })).toHaveLength(1)
    expect(input[1].latitude).toBe(50.14)
    expect(() => consolidateLocations(input, { coordinateCorrections: [{ ...correction, id: 'missing' }] })).toThrow('Stale')
    expect(() => consolidateLocations(input, { coordinateCorrections: [{ ...correction, latitude: 91 }] })).toThrow('Invalid')
    expect(() => consolidateLocations(input, { coordinateCorrections: [{ ...correction, evidence: '' }] })).toThrow('Invalid')
  })

  it('resolves reviewed distant duplicate IDs and fixes the Damp beach location', () => {
    expect(getSpot('spot-zt779e')?.id).toBe('spot-1c0jzo3')
    expect(getSpot('spot-xemepb')).toMatchObject({ id: 'spot-25uclm', latitude: 54.586445, longitude: 10.02496 })
  })
})
