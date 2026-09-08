import { describe, expect, it } from 'vitest'

import {
  buildReviewQueue,
  createReviewDecision,
  normalizeDecisionEnvelope,
} from '../src/spot-review/reviewState'

const candidate = {
  id: 'osm:node/1', name: 'Test Club', latitude: 52, longitude: 5,
  source: 'osm', sourceRef: 'https://www.openstreetmap.org/node/1',
  activities: ['sailing'], featureType: 'club',
}
const result = {
  candidateId: candidate.id, outcome: 'needs-review', reasons: ['water-not-found'],
  evidenceFingerprint: 'fingerprint-a', timezone: 'Europe/Amsterdam', countryCode: 'nl',
}

describe('spot review state', () => {
  it('shows unresolved candidates with their validation evidence', () => {
    expect(buildReviewQueue([candidate], [result], [])).toEqual([{
      ...candidate,
      validation: result,
      previousDecision: null,
    }])
  })

  it('hides matching decisions and reopens stale decisions', () => {
    const matching = createReviewDecision(candidate, result, { action: 'approve' })
    expect(buildReviewQueue([candidate], [result], [matching])).toEqual([])
    const stale = { ...matching, evidenceFingerprint: 'fingerprint-old' }
    expect(buildReviewQueue([candidate], [result], [stale]))
      .toEqual([expect.objectContaining({ previousDecision: stale })])
  })

  it('preserves a stable Windpeek id while correcting name and position', () => {
    const first = createReviewDecision(candidate, result, { action: 'approve' })
    const corrected = createReviewDecision(candidate, result, {
      action: 'approve',
      name: 'Corrected Club', latitude: 52.1, longitude: 5.1,
      windpeekId: first.windpeekId,
    })
    expect(corrected).toMatchObject({
      candidateId: 'osm:node/1', action: 'approve', name: 'Corrected Club',
      latitude: 52.1, longitude: 5.1, windpeekId: first.windpeekId,
    })
  })

  it('requires a rejection reason and valid coordinates for approval', () => {
    expect(() => createReviewDecision(candidate, result, { action: 'reject' })).toThrow('rejection reason')
    expect(() => createReviewDecision(candidate, result, { action: 'approve', latitude: 200 })).toThrow('coordinates')
  })

  it('normalizes malformed persisted decisions without corrupting valid entries', () => {
    const valid = createReviewDecision(candidate, result, { action: 'approve' })
    expect(normalizeDecisionEnvelope({ version: 1, decisions: [valid, { action: 'approve' }] }))
      .toEqual({ version: 1, decisions: [valid] })
  })
})
