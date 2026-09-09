import { describe, expect, it, vi } from 'vitest'

import { detectDuplicates, selectDuplicateSuppressions } from '../scripts/spots/lib/duplicate-detection.mjs'
import {
  cacheKeyForCandidate,
  collectGeoapifyEvidence,
  requiredGeoapifyCredits,
} from '../scripts/spots/lib/geoapify-validation.mjs'
import { classifyCandidate } from '../scripts/spots/lib/spot-validation.mjs'

function candidate(overrides = {}) {
  return {
    id: 'osm:node/1', source: 'osm', sourceId: 'node/1', name: 'Test Spot', country: 'NL',
    latitude: 52, longitude: 5, activities: ['sailing'], featureType: 'club',
    sourceRef: 'https://www.openstreetmap.org/node/1', releaseEligible: true, flags: [],
    ...overrides,
  }
}

function evidence(overrides = {}) {
  return {
    reverse: { countryCode: 'nl', timezone: 'Europe/Amsterdam' },
    water: { nearby: true, distanceMeters: 100, category: 'natural.water' },
    ...overrides,
  }
}

describe('deterministic spot validation', () => {
  it('automatically accepts a valid candidate beside water', () => {
    expect(classifyCandidate(candidate(), evidence())).toMatchObject({ outcome: 'accepted', reasons: [] })
  })

  it('routes missing nearby water to review rather than rejection', () => {
    expect(classifyCandidate(candidate(), evidence({ water: { nearby: false } })))
      .toMatchObject({ outcome: 'needs-review', reasons: ['water-not-found'] })
  })

  it('trusts curated source coordinates while still requiring a timezone', () => {
    expect(classifyCandidate(candidate({ source: 'varun' }), evidence({
      reverse: { countryCode: '', timezone: 'Europe/Amsterdam' },
      water: undefined,
    }), { trustedLocation: true, duplicateReasons: ['duplicate:within-75m'] }))
      .toMatchObject({ outcome: 'accepted', reasons: [], trustedLocation: true })

    expect(classifyCandidate(candidate({ source: 'varun' }), evidence({
      reverse: { countryCode: '', timezone: '' },
      water: undefined,
    }), { trustedLocation: true }))
      .toMatchObject({ outcome: 'needs-review', reasons: ['timezone-invalid'] })
  })

  it.each([
    [{ reverse: { countryCode: '', timezone: 'Europe/Amsterdam' } }, 'country-missing'],
    [{ reverse: { countryCode: 'be', timezone: 'Europe/Amsterdam' } }, 'country-conflict'],
    [{ reverse: { countryCode: 'nl', timezone: 'Not/AZone' } }, 'timezone-invalid'],
  ])('routes uncertain reverse evidence to review', (override, reason) => {
    const result = classifyCandidate(candidate(), evidence(override))
    expect(result.outcome).toBe('needs-review')
    expect(result.reasons).toContain(reason)
  })

  it('rejects inspection-only source records from release validation', () => {
    expect(classifyCandidate(candidate({ releaseEligible: false }), evidence()))
      .toMatchObject({ outcome: 'rejected', reasons: ['source-rights'] })
  })

  it('rejects names that cannot render on the device', () => {
    expect(classifyCandidate(candidate({ name: 'x'.repeat(100) }), evidence(), { trustedLocation: true }))
      .toMatchObject({ outcome: 'rejected', reasons: ['renderer-text-invalid'] })
  })

  it('includes evidence changes in the durable fingerprint', () => {
    const first = classifyCandidate(candidate(), evidence())
    const second = classifyCandidate(candidate(), evidence({ water: { nearby: true, distanceMeters: 900 } }))
    expect(first.evidenceFingerprint).not.toBe(second.evidenceFingerprint)
  })

  it('flags close coordinates and equivalent nearby names without merging', () => {
    const spots = [
      candidate({ id: 'osm:node/1', name: 'Chałupy', latitude: 54.7600, longitude: 18.4900 }),
      candidate({ id: 'osm:node/2', name: 'Chalupy', latitude: 54.7603, longitude: 18.4902 }),
      candidate({ id: 'osm:node/3', name: 'Chałupy East', latitude: 54.79, longitude: 18.49 }),
    ]
    const groups = detectDuplicates(spots)
    expect(groups).toEqual(expect.arrayContaining([
      expect.objectContaining({ leftId: 'osm:node/1', rightId: 'osm:node/2', reasons: expect.arrayContaining(['within-75m', 'equivalent-name-within-5km']) }),
    ]))
    expect(spots).toHaveLength(3)
  })

  it('retains distinct nearby breaks and same names in different regions', () => {
    const spots = [
      candidate({ id: 'a', name: 'Pipeline', featureType: 'surf-break' }),
      candidate({ id: 'b', name: 'Backdoor', latitude: 52.0001, featureType: 'surf-break' }),
      candidate({ id: 'c', name: 'Pipeline', latitude: 53 }),
    ]
    expect(detectDuplicates(spots)).toHaveLength(1)
    expect([...selectDuplicateSuppressions(spots, detectDuplicates(spots))]).toEqual([])
  })

  it('does not merge a chain whose endpoints are more than 5 km apart', () => {
    const spots = [
      candidate({ id: 'a', latitude: 52 }),
      candidate({ id: 'b', latitude: 52.04 }),
      candidate({ id: 'c', latitude: 52.08 }),
    ]
    expect([...selectDuplicateSuppressions(spots, detectDuplicates(spots))]).toEqual(['b'])
    expect([...selectDuplicateSuppressions([...spots].reverse(), detectDuplicates(spots))]).toEqual(['b'])
  })

  it('merges beach-name variants but preserves point versus beach and distant namesakes', () => {
    const spots = [
      candidate({ id: 'a', name: 'Praia da Armação', featureType: 'surf-break' }),
      candidate({ id: 'b', name: 'Armacao', latitude: 52.01, featureType: 'surf-break' }),
      candidate({ id: 'c', name: 'Armacao', latitude: 53, featureType: 'surf-break' }),
      candidate({ id: 'd', name: 'Point Plomer', featureType: 'surf-break' }),
      candidate({ id: 'e', name: 'Point Plomer Beach', latitude: 52.001, featureType: 'surf-break' }),
    ]
    expect([...selectDuplicateSuppressions(spots, detectDuplicates(spots))]).toEqual(['b'])
  })

  it('consolidates adjacent non-break facilities into one forecast location', () => {
    const spots = [candidate({ id: 'a', name: 'Sailing Club' }), candidate({ id: 'b', name: 'Sailing School', latitude: 52.0001 })]
    expect([...selectDuplicateSuppressions(spots, detectDuplicates(spots))]).toEqual(['b'])
  })

  it('automatically keeps one forecast location per duplicate group', () => {
    const spots = [
      candidate({ id: 'osm:node/1', source: 'osm', featureType: 'club' }),
      candidate({ id: 'osm:node/2', source: 'osm', featureType: 'watersport-location', latitude: 52.0001 }),
      candidate({ id: 'varun:1', source: 'varun', featureType: 'spot-collection', latitude: 52.0002 }),
    ]
    const suppressed = selectDuplicateSuppressions(spots, detectDuplicates(spots))
    expect([...suppressed].sort()).toEqual(['osm:node/1', 'osm:node/2'])
    expect(classifyCandidate(spots[0], evidence(), {
      trustedLocation: true,
      duplicateSuppressed: true,
    })).toMatchObject({ outcome: 'rejected', reasons: ['duplicate-suppressed'] })
  })
})

describe('Geoapify evidence collection', () => {
  it('counts only missing endpoint evidence during preflight', () => {
    const spots = [candidate(), candidate({ id: 'osm:node/2', latitude: 53 })]
    const cache = { [cacheKeyForCandidate(spots[0])]: { reverse: evidence().reverse } }
    expect(requiredGeoapifyCredits(spots, cache)).toBe(3)
    expect(requiredGeoapifyCredits(spots, cache, { includeWater: false })).toBe(1)
  })

  it('can collect timezone evidence without rechecking a trusted location', async () => {
    const fetchImpl = vi.fn(async () => ({
      ok: true,
      json: async () => ({ results: [{ country_code: 'nl', timezone: { name: 'Europe/Amsterdam' } }] }),
    }))
    const cache = {}
    await collectGeoapifyEvidence([candidate()], {
      cache, apiKey: 'test', fetchImpl, creditBudget: 1, delayMs: 0, includeWater: false,
    })
    expect(fetchImpl).toHaveBeenCalledTimes(1)
    expect(cache[cacheKeyForCandidate(candidate())]).toEqual({
      reverse: { countryCode: 'nl', timezone: 'Europe/Amsterdam' },
    })
  })

  it('makes zero calls when the conservative credit budget is exceeded', async () => {
    const fetchImpl = vi.fn()
    await expect(collectGeoapifyEvidence([candidate()], {
      cache: {}, apiKey: 'test', fetchImpl, creditBudget: 1,
    })).rejects.toThrow('requires 2 Geoapify credits')
    expect(fetchImpl).not.toHaveBeenCalled()
  })

  it('collects compact reverse and water evidence and reuses the cache', async () => {
    const fetchImpl = vi.fn(async (url) => {
      if (url.includes('/reverse')) return { ok: true, json: async () => ({ results: [{ country_code: 'nl', timezone: { name: 'Europe/Amsterdam' } }] }) }
      return { ok: true, json: async () => ({ features: [{ properties: { categories: ['natural.water'], distance: 120 } }] }) }
    })
    const cache = {}
    await collectGeoapifyEvidence([candidate()], { cache, apiKey: 'test', fetchImpl, creditBudget: 2, delayMs: 0 })
    await collectGeoapifyEvidence([candidate()], { cache, apiKey: 'test', fetchImpl, creditBudget: 2, delayMs: 0 })
    expect(fetchImpl).toHaveBeenCalledTimes(2)
    expect(cache[cacheKeyForCandidate(candidate())]).toEqual({
      reverse: { countryCode: 'nl', timezone: 'Europe/Amsterdam' },
      water: { nearby: true, distanceMeters: 120, category: 'natural.water' },
    })
  })

  it('retries a rate limit response and retains successful prior cache entries', async () => {
    const fetchImpl = vi.fn()
      .mockResolvedValueOnce({ ok: true, json: async () => ({ results: [{ country_code: 'nl', timezone: { name: 'Europe/Amsterdam' } }] }) })
      .mockResolvedValueOnce({ ok: false, status: 429, headers: { get: () => '0' } })
      .mockResolvedValueOnce({ ok: true, json: async () => ({ features: [] }) })
    const cache = {}
    const persist = vi.fn()
    await collectGeoapifyEvidence([candidate()], {
      cache, apiKey: 'test', fetchImpl, creditBudget: 2, delayMs: 0, sleep: async () => {}, persist,
    })
    expect(fetchImpl).toHaveBeenCalledTimes(3)
    expect(persist).toHaveBeenCalledTimes(2)
    expect(cache[cacheKeyForCandidate(candidate())].water).toEqual({ nearby: false })
  })
})
