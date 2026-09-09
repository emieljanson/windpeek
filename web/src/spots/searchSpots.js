export function normalizeSpotQuery(value) {
  return String(value ?? '')
    .normalize('NFKD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/[łŁ]/g, 'l')
    .replace(/æ/g, 'ae')
    .replace(/ø/g, 'o')
    .toLocaleLowerCase('en')
    .replace(/[^\p{L}\p{N}]+/gu, ' ')
    .trim()
}

export function searchSpots(spots, query, { limit = 20 } = {}) {
  const normalizedQuery = normalizeSpotQuery(query)
  if (normalizedQuery.length < 2) return []
  const personalNames = new Set(spots
    .filter((spot) => spot.personal)
    .map((spot) => normalizeSpotQuery(spot.name)))
  return spots
    .map((spot, index) => {
      const name = normalizeSpotQuery(spot.name)
      const matches = [spot.name, ...(spot.aliases ?? [])].map((value) => {
        const key = normalizeSpotQuery(value)
        return key === normalizedQuery ? 0 : key.startsWith(normalizedQuery) ? 1 : key.includes(normalizedQuery) ? 2 : 3
      })
      const score = Math.min(...matches)
      const match = score === 3 ? -1 : score
      return { spot, index, match, name }
    })
    .filter(({ match }) => match >= 0)
    .filter(({ spot, name }) => spot.personal || !personalNames.has(name))
    .sort((left, right) => (
      left.match - right.match ||
      Number(Boolean(right.spot.personal)) - Number(Boolean(left.spot.personal)) ||
      left.spot.name.localeCompare(right.spot.name) ||
      left.index - right.index
    ))
    .slice(0, limit)
    .map(({ spot }) => spot)
}
