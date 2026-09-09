import { describe, expect, it } from 'vitest'
import { parseSurfForecastLocation, importSurfForecastRecords } from '../scripts/spots/lib/surf-forecast-source.mjs'

describe('Surf-Forecast source', () => {
  const location = { name: 'Meio da Praia', filename: 'Meioda-Praia', lat: -22.9389, lng: -42.4802, type: 'Beach', hidden: false }

  it('uses precise map coordinates and verifies the requested spot identity', () => {
    expect(parseSurfForecastLocation(location, 'Meioda-Praia', 'Brazil')).toMatchObject({
      name: 'Meio da Praia', latitude: -22.9389, longitude: -42.4802, country: 'Brazil', sourceId: 'Meioda-Praia',
    })
    expect(() => parseSurfForecastLocation(location, 'Another-Spot', 'Brazil')).toThrow('identity')
    expect(() => parseSurfForecastLocation(undefined, 'Meioda-Praia', 'Brazil')).toThrow()
    expect(() => parseSurfForecastLocation({ ...location, lat: null }, 'Meioda-Praia', 'Brazil')).toThrow('coordinates')
  })

  it('keeps public lesser-known spots and distinguishes source rights from extraction', () => {
    const record = parseSurfForecastLocation(location, 'Meioda-Praia', 'Brazil')
    const result = importSurfForecastRecords([record, { ...record, sourceId: 'LesserKnown', lesserKnown: true }])
    expect(result.candidates).toHaveLength(2)
    expect(result.candidates[0]).toMatchObject({ source: 'surf-forecast', activities: ['surfing'], releaseEligible: false })
    expect(result.exclusions).toHaveLength(0)
    expect(importSurfForecastRecords([record], { releaseEligible: true }).candidates[0].releaseEligible).toBe(true)
  })

  it('excludes reviewed bad locations and rejects missing coordinates without inventing zero', () => {
    const record = parseSurfForecastLocation(location, 'Meioda-Praia', 'Brazil')
    const exclusion = { sourceId: record.sourceId, reason: 'reviewed-duplicate' }
    expect(importSurfForecastRecords([record], { excludedRecords: [exclusion] }))
      .toMatchObject({ candidates: [], exclusions: [exclusion], failures: [] })
    expect(importSurfForecastRecords([{ ...record, latitude: null }]))
      .toMatchObject({ candidates: [], failures: [{ sourceId: record.sourceId, reason: 'invalid-coordinates' }] })
  })
})
