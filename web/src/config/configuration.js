import { resolveTimeFormat } from './localeTimeFormat'
import { DEFAULT_THRESHOLD } from '../renderer/contract'
import { validTimezone } from '../timezone'

export const CONFIGURATION_VERSION = 4
export const BOARD_ID = 'seeedstudio_reterminal_e1002'
export const BOARD_IDS = Object.freeze({
  E1001: 'seeedstudio_reterminal_e1001',
  E1002: BOARD_ID,
  E1003: 'seeedstudio_reterminal_e1003',
})
export const SUPPORTED_BOARD_IDS = Object.freeze(Object.values(BOARD_IDS))
export const DEVICE_OPTIONS = Object.freeze([
  Object.freeze({ value: BOARD_IDS.E1001, label: 'E1001' }),
  Object.freeze({ value: BOARD_IDS.E1002, label: 'E1002' }),
  Object.freeze({ value: BOARD_IDS.E1003, label: 'E1003' }),
])

const SPOT_ID_PATTERN = /^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$/
const TIMEZONE_PATTERN = /^[A-Za-z0-9._+-]+(?:\/[A-Za-z0-9._+-]+)+$|^Etc\/UTC$/
const TEXT_ENCODER = new TextEncoder()

export const TIME_FORMATS = Object.freeze(['24-hour', '12-hour'])
export const TEMPERATURE_UNITS = Object.freeze(['celsius', 'fahrenheit'])
export const TEMPERATURE_CHOICES = Object.freeze(['hide', ...TEMPERATURE_UNITS])

export const DEFAULT_DISPLAY_CONFIGURATION = Object.freeze({
  showThreshold: false,
  threshold: DEFAULT_THRESHOLD,
  showWeather: true,
  showTemperature: false,
  showTide: false,
  showDedicatedFooter: false,
  timeFormat: '24-hour',
  temperatureUnit: 'celsius',
})

export function createDefaultDisplayConfiguration(locale) {
  return {
    ...DEFAULT_DISPLAY_CONFIGURATION,
    timeFormat: resolveTimeFormat(locale),
  }
}

export function displayConfigurationFromStore(store) {
  return {
    version: CONFIGURATION_VERSION,
    showThreshold: store.showThreshold,
    treatment: store.showThreshold ? 'threshold-line' : 'solid',
    threshold: store.threshold,
    showWeather: store.showWeather,
    showTemperature: store.showTemperature,
    showTide: Boolean(store.showTide && store.tideAvailable !== false),
    showDedicatedFooter: store.showDedicatedFooter,
    timeFormat: store.timeFormat,
    temperatureUnit: store.temperatureUnit,
  }
}

function canonicalInstalledConfiguration(configuration) {
  const { spot, display } = configuration
  return [
    configuration.version,
    configuration.boardId,
    configuration.deviceTimezone,
    spot.id,
    spot.name,
    Number(spot.latitude).toFixed(6),
    Number(spot.longitude).toFixed(6),
    spot.timezone,
    configuration.forecastModel,
    display.showThreshold ? 1 : 0,
    display.threshold,
    display.showWeather ? 1 : 0,
    display.showTemperature ? 1 : 0,
    display.showTide ? 1 : 0,
    display.showDedicatedFooter ? 1 : 0,
    display.timeFormat,
    display.temperatureUnit,
  ].join('|')
}

export function installedConfigurationDigest(configuration) {
  let hash = 0xcbf29ce484222325n
  for (const byte of TEXT_ENCODER.encode(canonicalInstalledConfiguration(configuration))) {
    hash ^= BigInt(byte)
    hash = BigInt.asUintN(64, hash * 0x100000001b3n)
  }
  return hash.toString(16).padStart(16, '0')
}

export function validateInstalledConfiguration(configuration) {
  if (!configuration || configuration.version !== CONFIGURATION_VERSION ||
      !SUPPORTED_BOARD_IDS.includes(configuration.boardId) ||
      !validTimezone(configuration.deviceTimezone) || configuration.deviceTimezone.length > 63) return false
  const { spot, display } = configuration
  if (!spot || typeof spot.id !== 'string' || !SPOT_ID_PATTERN.test(spot.id) ||
      typeof spot.name !== 'string' || spot.name.length < 1 || spot.name.length > 64 ||
      !Number.isFinite(spot.latitude) || spot.latitude < -90 || spot.latitude > 90 ||
      !Number.isFinite(spot.longitude) || spot.longitude < -180 || spot.longitude > 180 ||
      typeof spot.timezone !== 'string' || spot.timezone.length > 63 ||
      !TIMEZONE_PATTERN.test(spot.timezone)) return false
  if (typeof configuration.forecastModel !== 'string' ||
      configuration.forecastModel.length < 1 || configuration.forecastModel.length > 31) return false
  if (!display || typeof display.showThreshold !== 'boolean' ||
      !Number.isInteger(display.threshold) || display.threshold < 0 || display.threshold > 99 ||
      typeof display.showWeather !== 'boolean' ||
      typeof display.showTemperature !== 'boolean' || typeof display.showTide !== 'boolean' ||
      typeof display.showDedicatedFooter !== 'boolean' ||
      !TIME_FORMATS.includes(display.timeFormat) ||
      !TEMPERATURE_UNITS.includes(display.temperatureUnit)) return false
  return typeof configuration.digest !== 'string' ||
    configuration.digest === installedConfigurationDigest(configuration)
}

export function createInstalledConfiguration({
  spot, modelId, display, boardId = BOARD_ID, deviceTimezone = spot?.timezone,
}, { allowInvalid = false } = {}) {
  const configuration = {
    version: CONFIGURATION_VERSION,
    boardId,
    deviceTimezone: String(deviceTimezone ?? ''),
    spot: {
      id: String(spot?.id ?? ''),
      name: String(spot?.name ?? ''),
      latitude: Number(spot?.latitude),
      longitude: Number(spot?.longitude),
      timezone: String(spot?.timezone ?? ''),
    },
    forecastModel: String(modelId ?? ''),
    display: {
      showThreshold: Boolean(display?.showThreshold),
      threshold: Number(display?.threshold),
      showWeather: Boolean(display?.showWeather),
      showTemperature: Boolean(display?.showTemperature),
      showTide: Boolean(display?.showTide),
      showDedicatedFooter: Boolean(display?.showDedicatedFooter),
      timeFormat: String(display?.timeFormat ?? ''),
      temperatureUnit: String(display?.temperatureUnit ?? ''),
    },
  }
  configuration.digest = installedConfigurationDigest(configuration)
  if (!allowInvalid && !validateInstalledConfiguration(configuration)) {
    throw new TypeError('Invalid Windscout installation configuration')
  }
  return configuration
}
