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

function matchRank(value, query) {
  if (value === query) return 0
  if (value.startsWith(query)) return 1
  if (value.includes(` ${query}`)) return 2
  if (value.includes(query)) return 3
  return -1
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
      const nameRank = matchRank(name, normalizedQuery)
      const aliasRanks = (spot.aliases ?? [])
        .map((alias) => matchRank(normalizeSpotQuery(alias), normalizedQuery))
        .filter((rank) => rank >= 0)
      // A visible name match should lead an alias-only result of similar quality.
      const nameScore = nameRank >= 0 ? [0, 1, 2, 6][nameRank] : Infinity
      const aliasScore = aliasRanks.length ? [3, 4, 5, 7][Math.min(...aliasRanks)] : Infinity
      const score = Math.min(nameScore, aliasScore)
      const match = Number.isFinite(score) ? score : -1
      return { spot, index, match, name }
    })
    .filter(({ match }) => match >= 0)
    .filter(({ spot, name }) => spot.personal || !personalNames.has(name))
    .sort((left, right) => (
      left.match - right.match ||
      Number(Boolean(right.spot.personal)) - Number(Boolean(left.spot.personal)) ||
      left.name.length - right.name.length ||
      left.spot.name.localeCompare(right.spot.name) ||
      left.index - right.index
    ))
    .slice(0, limit)
    .map(({ spot }) => spot)
}
