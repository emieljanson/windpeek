import { DEFAULT_DISPLAY_CONFIGURATION } from '../config/configuration'
import { siteDisplayDefaults, siteVariant } from './siteVariant'

export function landingDisplayConfiguration(tide, variant = siteVariant(), swell = null, swellStatus = 'idle') {
  return {
    ...DEFAULT_DISPLAY_CONFIGURATION,
    ...siteDisplayDefaults(variant),
    swell, swellStatus,
    showTide: tide?.capability === 'available',
    tide,
    timeFormat: '24-hour',
  }
}
