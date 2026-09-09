import { describe, expect, it, vi } from 'vitest'

import {
  normalizeActivities,
  normalizeCandidate,
  parseGoogleCoordinates,
} from '../scripts/spots/lib/candidate-normalization.mjs'
import { importOsmElements } from '../scripts/spots/lib/osm-source.mjs'
import { importVarunRecords } from '../scripts/spots/lib/varun-source.mjs'

describe('spot source import', () => {
  it.each([
    ['https://www.google.com/maps?q=54.761960,18.498201', [54.76196, 18.498201]],
    ['https://maps.google.com/?query=-33.9253%2C18.4239', [-33.9253, 18.4239]],
    ['https://www.google.com/maps/place/Test/@28.1967,-14.1521,13z', [28.1967, -14.1521]],
    ['https://maps.google.com/?ll=52.1,5.2', [52.1, 5.2]],
  ])('parses coordinates from %s', (url, expected) => {
    expect(parseGoogleCoordinates(url)).toEqual({ latitude: expected[0], longitude: expected[1] })
  })

  it('normalizes supported semicolon-separated activities', () => {
    expect(normalizeActivities('sailing;kitesurfing;swimming;wing_foiling;surfing;kiteboarding'))
      .toEqual(['kitesurfing', 'sailing', 'surfing', 'wingfoil'])
  })

  it('imports direct Varun coordinates without copying descriptive content', async () => {
    const result = await importVarunRecords([{
      name: '  Chałupy 6 ',
      country: 'Poland',
      windguruUrl: 'https://www.windguru.cz/500757',
      locationUrl: 'https://www.google.com/maps?q=54.761960,18.498201',
      spotInfo: { description: 'Must not be copied' },
    }], { releaseEligible: false })

    expect(result.failures).toEqual([])
    expect(result.candidates).toHaveLength(1)
    expect(result.candidates[0]).toMatchObject({
      source: 'varun',
      sourceId: 'windguru-500757',
      name: 'Chałupy 6',
      country: 'Poland',
      latitude: 54.76196,
      longitude: 18.498201,
      activities: ['kitesurfing'],
      releaseEligible: false,
    })
    expect(JSON.stringify(result.candidates[0])).not.toContain('Must not be copied')
  })

  it('resolves a shortened Varun link once and reuses its cached coordinates', async () => {
    const fetchImpl = vi.fn(async () => ({
      url: 'https://www.google.com/maps?q=54.7,18.4',
      ok: true,
    }))
    const resolutions = {}
    const record = {
      name: 'Short link spot',
      country: 'Poland',
      windguruUrl: 'https://www.windguru.cz/123',
      locationUrl: 'https://maps.app.goo.gl/example',
    }

    const first = await importVarunRecords([record], { fetchImpl, resolutions, releaseEligible: false })
    const second = await importVarunRecords([record], { fetchImpl, resolutions, releaseEligible: false })

    expect(first.candidates[0]).toMatchObject({ latitude: 54.7, longitude: 18.4 })
    expect(second.candidates[0]).toMatchObject({ latitude: 54.7, longitude: 18.4 })
    expect(fetchImpl).toHaveBeenCalledTimes(1)
    expect(fetchImpl).toHaveBeenCalledWith(
      new URL('https://maps.app.goo.gl/example'),
      expect.objectContaining({ redirect: 'manual' }),
    )
  })

  it('never follows Varun location links outside the approved map hosts', async () => {
    const fetchImpl = vi.fn()
    const external = await importVarunRecords([{
      name: 'External', country: 'NL', locationUrl: 'https://example.com/location',
    }], { fetchImpl })
    expect(external.failures[0].reason).toBe('location-resolution-failed')
    expect(fetchImpl).not.toHaveBeenCalled()

    const redirectingFetch = vi.fn(async () => ({
      ok: false,
      status: 302,
      headers: { get: () => 'http://127.0.0.1/private' },
    }))
    const redirected = await importVarunRecords([{
      name: 'Redirect', country: 'NL', locationUrl: 'https://maps.app.goo.gl/redirect',
    }], { fetchImpl: redirectingFetch })
    expect(redirected.failures[0].reason).toBe('location-resolution-failed')
    expect(redirectingFetch).toHaveBeenCalledTimes(1)
  })

  it('reports malformed Varun rows without aborting valid rows', async () => {
    const result = await importVarunRecords([
      { name: '', country: 'NL', locationUrl: 'https://www.google.com/maps?q=52,5' },
      { name: 'Valid', country: 'NL', locationUrl: 'https://www.google.com/maps?q=52,5' },
    ], { releaseEligible: false })

    expect(result.candidates).toHaveLength(1)
    expect(result.failures).toHaveLength(1)
    expect(result.failures[0].reason).toBe('missing-name')
  })

  it('keeps physical OSM watersport features and excludes shops', () => {
    const result = importOsmElements([
      {
        type: 'node', id: 1, lat: 52.1, lon: 5.1,
        tags: { name: 'Sailing Club', sport: 'sailing', club: 'sport' },
      },
      {
        type: 'node', id: 2, lat: 52.2, lon: 5.2,
        tags: { name: 'Boat Store', sport: 'sailing', shop: 'boat' },
      },
      {
        type: 'way', id: 3, center: { lat: 51.9, lon: 4.1 },
        tags: { name: 'Wind Beach', sport: 'windsurfing;kitesurfing', natural: 'beach' },
      },
    ], { releaseEligible: true })

    expect(result.candidates.map((candidate) => candidate.name)).toEqual(['Sailing Club', 'Wind Beach'])
    expect(result.exclusions).toEqual([expect.objectContaining({ sourceId: 'node/2', reason: 'non-physical-facility' })])
    expect(result.candidates[1].activities).toEqual(['kitesurfing', 'windsurfing'])
  })

  it('routes very large OSM geometries to review', () => {
    const result = importOsmElements([{
      type: 'relation', id: 4,
      center: { lat: 52, lon: 5 },
      bounds: { minlat: 51, minlon: 4, maxlat: 53, maxlon: 6 },
      tags: { name: 'Large sailing area', sport: 'sailing', leisure: 'marina' },
    }], { releaseEligible: true })

    expect(result.candidates[0].flags).toContain('large-geometry')
  })

  it('creates deterministic identities and coordinate precision', () => {
    const candidate = normalizeCandidate({
      source: 'osm', sourceId: 'node/99', name: '  Test  Spot ', country: 'NL',
      latitude: 52.123456789, longitude: 5.987654321, activities: ['sailing'],
      featureType: 'club', sourceRef: 'https://www.openstreetmap.org/node/99', releaseEligible: true,
    })
    expect(candidate).toMatchObject({
      id: 'osm:node/99', name: 'Test Spot', latitude: 52.123457, longitude: 5.987654,
    })
  })
})
