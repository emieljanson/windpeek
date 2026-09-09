import { candidateFitsRenderer } from './spot-validation.mjs'

function foldName(value) {
  return String(value ?? '')
    .normalize('NFKD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/[łŁ]/g, 'l')
    .replace(/æ/g, 'ae')
    .replace(/ø/g, 'o')
    .toLocaleLowerCase('en')
    .replace(/[^\p{L}\p{N}]+/gu, '')
}

const FEATURE_PRIORITY = new Map([
  ['spot-collection', 0],
  ['watersport-location', 1],
  ['beach', 2],
  ['launch', 3],
  ['marina', 4],
  ['club', 5],
  ['sports-centre', 6],
])

export function placeName(value) {
  return foldName(String(value).normalize('NFKD').replace(/[\u0300-\u036f]/g, '')
    .replace(/\b(praia|playa|plage|beach|de|da|do|del|la|le|the)\b/gi, ''))
}

function isBreak(candidate) {
  return candidate.featureType === 'surf-break' || candidate.activities?.includes('surfing')
}

function compareDuplicateCandidates(left, right) {
  return Number(candidateFitsRenderer(right)) - Number(candidateFitsRenderer(left)) ||
    Number(right.releaseEligible === true) - Number(left.releaseEligible === true) ||
    Number(right.source === 'varun') - Number(left.source === 'varun') ||
    (FEATURE_PRIORITY.get(left.featureType) ?? 99) - (FEATURE_PRIORITY.get(right.featureType) ?? 99) ||
    left.id.localeCompare(right.id)
}

export function distanceMeters(left, right) {
  const radians = (degrees) => degrees * Math.PI / 180
  const lat1 = radians(left.latitude)
  const lat2 = radians(right.latitude)
  const deltaLat = radians(right.latitude - left.latitude)
  const deltaLon = radians(right.longitude - left.longitude)
  const a = Math.sin(deltaLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(deltaLon / 2) ** 2
  const bounded = Math.max(0, Math.min(1, a))
  return 6371000 * 2 * Math.atan2(Math.sqrt(bounded), Math.sqrt(1 - bounded))
}

export function detectDuplicates(candidates) {
  const groups = []
  const foldedNames = candidates.map((candidate) => foldName(candidate.name))
  const placeNames = candidates.map((candidate) => placeName(candidate.name))
  for (let leftIndex = 0; leftIndex < candidates.length; leftIndex += 1) {
    const left = candidates[leftIndex]
    for (let rightIndex = leftIndex + 1; rightIndex < candidates.length; rightIndex += 1) {
      const right = candidates[rightIndex]
      if (Math.abs(left.latitude - right.latitude) > 0.046) continue
      const distance = distanceMeters(left, right)
      if (distance > 5000) continue
      const reasons = []
      if (distance <= 75) reasons.push('within-75m')
      if (foldedNames[leftIndex] && foldedNames[leftIndex] === foldedNames[rightIndex]) reasons.push('equivalent-name-within-5km')
      // Keep a named point and its neighbouring beach distinct.
      const pointAndBeach = /\bpoint\b/i.test(`${left.name} ${right.name}`) &&
        /\bbeach\b/i.test(left.name) !== /\bbeach\b/i.test(right.name)
      if (distance <= 3000 && placeNames[leftIndex].length >= 4 &&
          placeNames[leftIndex] === placeNames[rightIndex] && !pointAndBeach) reasons.push('place-name-variant-within-3km')
      if (distance <= 75 && !isBreak(left) && !isBreak(right)) reasons.push('same-forecast-location-within-75m')
      if (reasons.length) groups.push({
        leftId: left.id,
        rightId: right.id,
        distanceMeters: Math.round(distance),
        reasons,
      })
    }
  }
  return groups.sort((left, right) => `${left.leftId}:${left.rightId}`.localeCompare(`${right.leftId}:${right.rightId}`))
}

export function selectDuplicateSuppressions(candidates, groups) {
  // Adjacent surf breaks need a name match; co-located facilities share a forecast.
  const matches = new Map(candidates.map((candidate) => [candidate.id, new Set()]))
  for (const group of groups) {
    if (!group.reasons.some((reason) => ['equivalent-name-within-5km', 'place-name-variant-within-3km', 'same-forecast-location-within-75m'].includes(reason))) continue
    if (!matches.has(group.leftId) || !matches.has(group.rightId)) continue
    matches.get(group.leftId).add(group.rightId)
    matches.get(group.rightId).add(group.leftId)
  }
  const suppressed = new Set()
  // Each removal must match a retained winner directly, never through a chain.
  for (const candidate of [...candidates].sort(compareDuplicateCandidates)) {
    if (suppressed.has(candidate.id)) continue
    for (const duplicateId of matches.get(candidate.id)) suppressed.add(duplicateId)
  }
  return suppressed
}
