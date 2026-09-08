import { stableSpotId } from '../spots/spotIdentity'

function validCoordinates(latitude, longitude) {
  return Number.isFinite(latitude) && latitude >= -90 && latitude <= 90 &&
    Number.isFinite(longitude) && longitude >= -180 && longitude <= 180
}

function validDecision(decision) {
  if (!decision || typeof decision.candidateId !== 'string' || !decision.candidateId) return false
  if (typeof decision.evidenceFingerprint !== 'string' || !decision.evidenceFingerprint) return false
  if (!['approve', 'reject'].includes(decision.action)) return false
  if (decision.action === 'reject') return typeof decision.reason === 'string' && Boolean(decision.reason.trim())
  return typeof decision.windpeekId === 'string' && Boolean(decision.windpeekId) &&
    typeof decision.name === 'string' && Boolean(decision.name.trim()) &&
    validCoordinates(Number(decision.latitude), Number(decision.longitude))
}

export function normalizeDecisionEnvelope(value) {
  const decisions = Array.isArray(value?.decisions) ? value.decisions.filter(validDecision) : []
  return {
    version: 1,
    decisions: decisions
      .map((decision) => ({ ...decision }))
      .sort((left, right) => left.candidateId.localeCompare(right.candidateId)),
  }
}

export function buildReviewQueue(candidates, results, decisions) {
  const candidatesById = new Map(candidates.map((candidate) => [candidate.id, candidate]))
  const decisionsById = new Map(decisions.map((decision) => [decision.candidateId, decision]))
  return results
    .filter((result) => result.outcome === 'needs-review')
    .flatMap((result) => {
      const candidate = candidatesById.get(result.candidateId)
      if (!candidate) return []
      const previousDecision = decisionsById.get(result.candidateId) ?? null
      if (previousDecision?.evidenceFingerprint === result.evidenceFingerprint) return []
      return [{ ...candidate, validation: result, previousDecision }]
    })
    .sort((left, right) => left.name.localeCompare(right.name))
}

export function createReviewDecision(candidate, validation, {
  action,
  name = candidate.name,
  latitude = candidate.latitude,
  longitude = candidate.longitude,
  windpeekId = stableSpotId(candidate.id),
  reason = '',
} = {}) {
  if (!['approve', 'reject'].includes(action)) throw new Error('Choose approve or reject.')
  const base = {
    candidateId: candidate.id,
    evidenceFingerprint: validation.evidenceFingerprint,
    action,
  }
  if (action === 'reject') {
    if (!String(reason).trim()) throw new Error('Enter a rejection reason.')
    return { ...base, reason: String(reason).trim() }
  }
  const cleanName = String(name).trim().replace(/\s+/g, ' ')
  const lat = Number(latitude)
  const lon = Number(longitude)
  if (!cleanName) throw new Error('Enter a spot name.')
  if (!validCoordinates(lat, lon)) throw new Error('Choose valid coordinates.')
  return {
    ...base,
    windpeekId,
    name: cleanName,
    latitude: Number(lat.toFixed(6)),
    longitude: Number(lon.toFixed(6)),
    timezone: validation.timezone || '',
  }
}
