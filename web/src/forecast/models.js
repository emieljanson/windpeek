const always = (id, label, screenLabel = label.toUpperCase()) => Object.freeze({
  id, apiId: id, label, screenLabel, availability: 'always',
})

const regional = (id, apiId, label, countryCodes, options = {}) => Object.freeze({
  id,
  apiId,
  label,
  screenLabel: options.screenLabel ?? label.toUpperCase(),
  availability: 'regional',
  countryCodes: Object.freeze(countryCodes),
  coordinateCheck: options.coordinateCheck,
})

const WORLDWIDE_MODELS = [
  always('best_match', 'Best Match'),
  always('ecmwf_ifs', 'ECMWF', 'ECMWF IFS'),
  always('icon_seamless', 'ICON', 'DWD ICON'),
  always('ncep_gfs_seamless', 'GFS', 'NOAA GFS'),
]

const DMI_COUNTRIES = [
  'at', 'be', 'ch', 'cz', 'de', 'dk', 'ee', 'fi', 'fr', 'gb', 'ie', 'is',
  'lt', 'lu', 'lv', 'nl', 'no', 'pl', 'se',
]

const REGIONAL_MODELS = [
  regional('knmi_harmonie', 'knmi_harmonie_arome_netherlands', 'HARM-NL', ['nl', 'be'], { screenLabel: 'KNMI HARMONIE' }),
  regional('dmi_harmonie', 'dmi_harmonie_arome_europe', 'HARM-DK', DMI_COUNTRIES, { screenLabel: 'DMI HARMONIE' }),
  regional('met_nordic', 'metno_nordic', 'MET Nordic', ['dk', 'fi', 'no', 'se']),
  regional('meteofrance_arome', 'meteofrance_arome_france_hd', 'AROME-FR', ['fr'], { screenLabel: 'MÉTÉO-FRANCE AROME' }),
  regional('ukmo_ukv', 'ukmo_uk_deterministic_2km', 'UKV', ['gb', 'ie'], { screenLabel: 'UK MET OFFICE UKV' }),
  regional('meteoswiss_icon', 'meteoswiss_icon_ch1', 'ICON-CH', ['ch', 'li'], { screenLabel: 'METEOSWISS ICON' }),
  regional('geosphere_arome', 'geosphere_arome_austria', 'AROME-AT', ['at'], { screenLabel: 'GEOSPHERE AROME' }),
  regional('italiameteo_icon', 'italia_meteo_arpae_icon_2i', 'ICON-2I', ['it'], { screenLabel: 'ITALIAMETEO ICON' }),
  regional('chmi_aladin', 'chmi_aladin_cz_1km', 'ALADIN', ['cz'], { screenLabel: 'CHMI ALADIN' }),
  regional('noaa_hrrr', 'ncep_hrrr_conus', 'HRRR', ['us'], {
    screenLabel: 'NOAA HRRR',
    coordinateCheck: ({ latitude, longitude }) =>
      latitude >= 24 && latitude <= 50 && longitude >= -125 && longitude <= -66,
  }),
  regional('canada_hrdps', 'cmc_gem_hrdps', 'HRDPS', ['ca'], { screenLabel: 'ENVIRONMENT CANADA HRDPS' }),
  regional('jma_msm', 'jma_msm', 'MSM', ['jp'], { screenLabel: 'JMA MSM' }),
  regional('kma_ldps', 'kma_ldps', 'LDPS', ['kr'], { screenLabel: 'KMA LDPS' }),
]

export const FORECAST_MODELS = Object.freeze([...WORLDWIDE_MODELS, ...REGIONAL_MODELS])

export const ALWAYS_FORECAST_MODEL_IDS = Object.freeze(WORLDWIDE_MODELS.map((model) => model.id))
export const DEFAULT_FORECAST_MODEL_ID = 'best_match'

export function getForecastModel(modelId) {
  return FORECAST_MODELS.find((model) => model.id === modelId) ?? null
}

export function forecastModelsForSpot(spot) {
  const countryCode = String(spot?.countryCode ?? '').toLowerCase()
  const latitude = Number(spot?.latitude)
  const longitude = Number(spot?.longitude)
  const regionalModels = REGIONAL_MODELS.filter((model) =>
    model.countryCodes.includes(countryCode) &&
    (!model.coordinateCheck || model.coordinateCheck({ latitude, longitude })))
  return [...WORLDWIDE_MODELS, ...regionalModels]
}
