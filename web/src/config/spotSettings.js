// Only display choices belong to a spot. Keep network, preview and device state out.
export const SPOT_SETTING_FIELDS = Object.freeze([
  'showThreshold', 'threshold', 'showWeather', 'showTemperature', 'showTide',
  'showDedicatedFooter', 'timeFormat', 'temperatureUnit', 'windSize', 'swellSize',
  'selectedModelId', 'selectedSwellModelId', 'moduleOrder',
])

export function captureSpotSettings(source) {
  return Object.fromEntries(SPOT_SETTING_FIELDS.map(key => [
    key, Array.isArray(source[key]) ? [...source[key]] : source[key],
  ]))
}

export function settingsForSpot(store, spotId) {
  const settings = captureSpotSettings(store)
  if (spotId !== store.selectedSpotId) Object.assign(settings, store.spotSettings[spotId])
  return settings
}
