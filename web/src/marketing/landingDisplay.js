import { DEFAULT_DISPLAY_CONFIGURATION } from '../config/configuration'

export function landingDisplayConfiguration(tide) {
  return {
    ...DEFAULT_DISPLAY_CONFIGURATION,
    showThreshold: true,
    showTemperature: true,
    showTide: tide?.capability === 'available',
    tide,
    timeFormat: '24-hour',
  }
}
