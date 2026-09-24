import generatedCatalog from './spots/catalog.runtime.generated.json'

export const DEFAULT_SPOT_ID = 'brouwersdam'

export const SPOTS = Object.freeze(generatedCatalog.map(([
  id, name, latitude, longitude, timezone, countryCode, aliases, aliasIds,
]) => Object.freeze({ id, name, displayName: name.toUpperCase(), latitude, longitude, timezone, countryCode,
  ...(aliases ? { aliases, aliasIds } : {}),
})))

const SPOTS_BY_ID = new Map(SPOTS.map((spot) => [spot.id, spot]))
for (const spot of SPOTS) for (const id of spot.aliasIds ?? []) {
  if (!SPOTS_BY_ID.has(id)) SPOTS_BY_ID.set(id, spot)
}

export function getSpot(spotId) {
  return SPOTS_BY_ID.get(spotId) ?? null
}
