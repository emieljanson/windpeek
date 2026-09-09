import { describe, expect, it } from 'vitest'

import { buildRuntimeCatalog } from '../scripts/spots/lib/catalog-builder.mjs'
import { buildNearbyIndex } from '../scripts/spots/lib/nearby-index.mjs'
import { verifyReleaseSources } from '../scripts/spots/lib/release-gates.mjs'
import { searchSpots } from '../src/spots/searchSpots'
import { stableSpotId } from '../src/spots/spotIdentity'
import worldwideCatalog from '../src/spots/catalog.generated.json'

const existing = [
  { id: 'edam', name: 'Edam', displayName: 'EDAM', latitude: 52.5126, longitude: 5.0486, timezone: 'Europe/Amsterdam', countryCode: 'nl' },
  { id: 'brouwersdam', name: 'Brouwersdam', displayName: 'BROUWERSDAM', latitude: 51.7506, longitude: 3.8577, timezone: 'Europe/Amsterdam', countryCode: 'nl' },
  { id: 'castricum-aan-zee', name: 'Castricum aan Zee', displayName: 'CASTRICUM AAN ZEE', latitude: 52.555, longitude: 4.609, timezone: 'Europe/Amsterdam', countryCode: 'nl' },
]
const candidate = {
  id: 'osm:node/99', name: 'Chałupy', latitude: 54.76, longitude: 18.49,
  releaseEligible: true,
}
const accepted = {
  candidateId: candidate.id, outcome: 'accepted', timezone: 'Europe/Warsaw',
  countryCode: 'pl', evidenceFingerprint: 'current',
}

describe('runtime spot catalog', () => {
  it('rejects the same curated location listed under both a current and former ID', () => {
    expect(() => buildNearbyIndex({
      catalog: [{ ...existing[0], aliasIds: ['old-edam'] }],
      popularSpots: [{ id: 'edam', name: 'Edam', priority: 2 }, { id: 'old-edam', name: 'Edam', priority: 3 }],
    })).toThrow('listed more than once')
  })
  it('uses current country codes and preserves Hong Kong and Macau regions', () => {
    for (const [name, countryCode] of [
      ["Aber Wrac'h Point", 'fr'], ['Aberaeron', 'gb'], ['Adler - Mzymta River', 'ru'],
      ['Back Wash', 'bj'], ['Big Wave Bay', 'hk'], ['Macau Hacs Sa Beach', 'mo'],
      ['Portrush-West Strand', 'gb'], ['Vama Veche', 'ro'], ['Gaza Harbourmouth', 'ps'],
    ]) expect(worldwideCatalog.find((spot) => spot.name === name || spot.aliases?.includes(name))?.countryCode, name).toBe(countryCode)
  })

  it('keeps existing ids and includes only accepted or currently approved records', () => {
    const reviewCandidate = { ...candidate, id: 'osm:node/100', name: 'Reviewed Spot' }
    const catalog = buildRuntimeCatalog({
      existing,
      candidates: [
        candidate,
        reviewCandidate,
        { ...candidate, id: 'varun:1', name: 'Rights hold', releaseEligible: false },
        { ...candidate, id: 'varun:2', name: 'Brouwersdam' },
      ],
      validationResults: [
        accepted,
        { ...accepted, candidateId: 'varun:2' },
        {
          candidateId: reviewCandidate.id, outcome: 'needs-review', timezone: 'Europe/Warsaw', evidenceFingerprint: 'review-current',
        },
      ],
      decisions: [{
        candidateId: reviewCandidate.id, action: 'approve', evidenceFingerprint: 'review-current',
        windpeekId: 'reviewed-spot', name: 'Reviewed Corrected', latitude: 54.7, longitude: 18.4,
        timezone: 'Europe/Warsaw',
      }],
    })

    expect(catalog.slice(0, 3).map((spot) => spot.id)).toEqual(['edam', 'brouwersdam', 'castricum-aan-zee'])
    expect(catalog).toEqual(expect.arrayContaining([
      expect.objectContaining({ name: 'Chałupy', timezone: 'Europe/Warsaw', countryCode: 'pl' }),
      expect.objectContaining({ id: 'reviewed-spot', name: 'Reviewed Corrected' }),
    ]))
    expect(catalog.some((spot) => spot.name === 'Rights hold')).toBe(false)
    expect(catalog.filter((spot) => spot.name === 'Brouwersdam')).toHaveLength(1)
  })

  it('does not reuse a stale review decision', () => {
    expect(buildRuntimeCatalog({
      existing: [], candidates: [candidate],
      validationResults: [{ ...accepted, outcome: 'needs-review' }],
      decisions: [{ candidateId: candidate.id, action: 'approve', evidenceFingerprint: 'old' }],
    })).toEqual([])
  })

  it.each([
    [{ ...candidate, latitude: 200 }, 'coordinates'],
    [{ ...candidate, name: 'x'.repeat(100) }, 'renderer'],
    [{ ...candidate, name: 'Bad\0Name' }, 'renderer'],
  ])('rejects invalid generated records', (invalid, message) => {
    expect(() => buildRuntimeCatalog({
      existing: [], candidates: [invalid], validationResults: [accepted], decisions: [],
    })).toThrow(message)
  })
})

describe('catalog search', () => {
  const curated = [
    { id: 'chalupy', name: 'Chałupy' },
    { id: 'cape-town', name: 'Cape Town' },
    { id: 'town-lake', name: 'Town Lake' },
  ]
  const personal = [{ id: 'personal-cape', name: 'Cape Town', personal: true }]

  it('folds accents, punctuation, and case', () => {
    expect(searchSpots([...curated, ...personal], 'chalupy').map((spot) => spot.id)).toContain('chalupy')
    expect(searchSpots([...curated, ...personal], 'CAPE-TOWN')[0].id).toBe('personal-cape')
  })

  it('ranks personal exact, curated exact, prefix, then substring matches', () => {
    expect(searchSpots([...curated, ...personal], 'cape').map((spot) => spot.id).slice(0, 2))
      .toEqual(['personal-cape'])
    expect(searchSpots([...curated, ...personal], 'town').map((spot) => spot.id))
      .toEqual(['town-lake', 'personal-cape'])
  })

  it('keeps same-name catalog spots unless a personal spot replaces them', () => {
    const duplicates = [
      { id: 'north', name: 'Kite Beach' },
      { id: 'south', name: 'Kite Beach' },
    ]
    expect(searchSpots(duplicates, 'kite')).toHaveLength(2)
    expect(searchSpots([...duplicates, { id: 'personal', name: 'Kite Beach', personal: true }], 'kite'))
      .toEqual([{ id: 'personal', name: 'Kite Beach', personal: true }])
  })

  it('does not expose the global catalog before two characters and caps results', () => {
    const many = Array.from({ length: 30 }, (_, index) => ({ id: `spot-${index}`, name: `Spot ${index}` }))
    expect(searchSpots(many, '')).toEqual([])
    expect(searchSpots(many, 's')).toEqual([])
    expect(searchSpots(many, 'spot')).toHaveLength(20)
  })
})

describe('nearby default index', () => {
  const surfCandidate = {
    id: 'varun:popular',
    name: 'Popular Beach',
    activities: ['kitesurfing'],
    featureType: 'spot-collection',
  }
  const surfSpot = { id: stableSpotId(surfCandidate.id), name: 'Popular Beach' }

  it('includes only explicitly curated places, not automatically imported candidates', () => {
    const dolphinBeach = {
      ...surfCandidate,
      id: 'varun:dolphin-beach',
      name: 'Dolphin Beach',
    }
    const dolphinBeachSpot = { id: stableSpotId(dolphinBeach.id), name: 'Dolphin Beach' }
    const excludedCandidates = [
      { ...surfCandidate, id: 'osm:club', featureType: 'club' },
      { ...surfCandidate, id: 'varun:school', name: 'Surfschule Timmendorfer Strand' },
      { ...surfCandidate, id: 'varun:centre', name: 'Wind Sport Center' },
      { ...surfCandidate, id: 'varun:camping', name: 'Surf camping Vietnam' },
      { ...surfCandidate, id: 'varun:launch', name: 'Kitesurf launch' },
      { ...surfCandidate, id: 'varun:generic', name: 'Kitesurf' },
      { ...surfCandidate, id: 'varun:generic-spaced', name: 'Kite Surfing' },
    ]
    expect(buildNearbyIndex({
      candidates: [
        surfCandidate,
        ...excludedCandidates,
        dolphinBeach,
      ],
      catalog: [
        surfSpot,
        dolphinBeachSpot,
        ...excludedCandidates.map((candidate) => ({
          id: stableSpotId(candidate.id),
          name: candidate.name,
        })),
      ],
      popularSpots: [{ ...surfSpot, priority: 3 }],
    })).toEqual([
      { id: surfSpot.id, priority: 3 },
    ].sort((left, right) => left.id.localeCompare(right.id)))
  })

  it.each(['beach', 'spot-collection', 'watersport-location'])('does not recommend a named %s without an explicit selection', (featureType) => {
    expect(buildNearbyIndex({
      candidates: [{ ...surfCandidate, featureType }],
      catalog: [surfSpot],
      popularSpots: [],
    })).toEqual([])
  })

  it('includes explicitly bundled places at baseline priority', () => {
    expect(buildNearbyIndex({
      candidates: [], catalog: existing, popularSpots: [],
      baselineSpotIds: ['edam', 'castricum-aan-zee'],
    })).toEqual([
      { id: 'castricum-aan-zee', priority: 1 },
      { id: 'edam', priority: 1 },
    ])
  })

  it('rejects missing baseline spots', () => {
    expect(() => buildNearbyIndex({
      candidates: [], catalog: existing, popularSpots: [], baselineSpotIds: ['missing'],
    })).toThrow('must be a named place in the catalog')
  })

  it('rejects duplicate curated entries instead of silently overriding them', () => {
    expect(() => buildNearbyIndex({
      candidates: [surfCandidate],
      catalog: [surfSpot],
      popularSpots: [
        { ...surfSpot, priority: 2 },
        { ...surfSpot, priority: 3 },
      ],
    })).toThrow('listed more than once')
  })
})

describe('catalog release gates', () => {
  it('requires release rights metadata for contributing sources', () => {
    const manifest = { sources: [{
      id: 'osm-snapshot', adapter: 'osm', releaseEligible: true,
      rights: { license: 'ODbL-1.0', redistribution: true, attribution: '© OpenStreetMap contributors' },
    }] }
    expect(() => verifyReleaseSources({
      manifest,
      candidates: [{ source: 'osm', releaseEligible: true }],
    })).not.toThrow()
    expect(() => verifyReleaseSources({
      manifest: { sources: [{
        id: 'osm-snapshot', adapter: 'osm', releaseEligible: true,
        rights: { license: '', redistribution: true, attribution: '' },
      }] },
      candidates: [{ source: 'osm', releaseEligible: true }],
    })).toThrow('rights metadata')
  })

  it('fails closed when dataset redistribution rights are unresolved', () => {
    expect(() => verifyReleaseSources({
      manifest: { sources: [{
        id: 'varun', adapter: 'varun', releaseEligible: false,
        rights: { license: 'unconfirmed', redistribution: false, attribution: 'Varun' },
      }] },
      candidates: [{ source: 'varun', releaseEligible: true }],
    })).toThrow('not release-eligible')
  })
})
