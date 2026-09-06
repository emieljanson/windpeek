import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  BOARD_ID,
  CONFIGURATION_VERSION,
  createInstalledConfiguration,
  createDefaultDisplayConfiguration,
  displayConfigurationFromStore,
  installedConfigurationDigest,
  validateInstalledConfiguration,
} from '../src/config/configuration'
import { resolveTimeFormat } from '../src/config/localeTimeFormat'

describe('display configuration', () => {
  afterEach(() => vi.restoreAllMocks())

  it('keeps version 3 output while deriving its treatment from threshold visibility', () => {
    const settings = {
      showThreshold: false,
      threshold: 23,
      showWeather: true,
      showTemperature: false,
      showTide: false,
      showDedicatedFooter: true,
      timeFormat: '24-hour',
      temperatureUnit: 'celsius',
    }

    expect(displayConfigurationFromStore(settings)).toEqual({
      version: CONFIGURATION_VERSION,
      showThreshold: false,
      treatment: 'solid',
      threshold: 23,
      showWeather: true,
      showTemperature: false,
      showTide: false,
      showDedicatedFooter: true,
      timeFormat: '24-hour',
      temperatureUnit: 'celsius',
    })
    expect(displayConfigurationFromStore({ ...settings, showThreshold: true }).treatment)
      .toBe('threshold-line')
  })

  it('never installs Tide enabled when it is unavailable for the selected spot', () => {
    const settings = {
      showThreshold: false,
      threshold: 17,
      showWeather: true,
      showTemperature: false,
      showTide: true,
      showDedicatedFooter: true,
      tideAvailable: false,
      timeFormat: '24-hour',
      temperatureUnit: 'celsius',
    }

    expect(displayConfigurationFromStore(settings).showTide).toBe(false)
    expect(displayConfigurationFromStore({ ...settings, tideAvailable: true }).showTide).toBe(true)
  })

  it('preserves threshold visibility when store settings become an installation', () => {
    const display = displayConfigurationFromStore({
      showThreshold: true,
      threshold: 23,
      showWeather: true,
      showTemperature: false,
      showTide: false,
      showDedicatedFooter: true,
      timeFormat: '24-hour',
      temperatureUnit: 'celsius',
    })
    const configuration = createInstalledConfiguration({
      spot: {
        id: 'brouwersdam',
        name: 'Brouwersdam',
        latitude: 51.7506,
        longitude: 3.8577,
        timezone: 'Europe/Amsterdam',
      },
      modelId: 'best_match',
      display,
    })

    expect(configuration.display.showThreshold).toBe(true)
    expect(configuration.digest).not.toBe('f70d51b9a49fdddb')
  })

  it.each([
    [true, '12-hour'],
    [false, '24-hour'],
  ])('resolves hour12=%s to %s', (hour12, expected) => {
    const resolvedOptions = vi.fn(() => ({ hour12 }))
    const DateTimeFormat = vi.spyOn(Intl, 'DateTimeFormat')
      .mockImplementation(function MockDateTimeFormat() {
        return { resolvedOptions }
      })

    expect(resolveTimeFormat('nl-NL')).toBe(expected)
    expect(DateTimeFormat).toHaveBeenCalledWith('nl-NL', { hour: 'numeric' })
    expect(resolvedOptions).toHaveBeenCalledOnce()
  })

  it('creates active defaults with solid bars, a retained threshold, and the locale convention', () => {
    vi.spyOn(Intl, 'DateTimeFormat').mockImplementation(function MockDateTimeFormat() {
      return { resolvedOptions: () => ({ hour12: true }) }
    })

    expect(createDefaultDisplayConfiguration('en-US')).toMatchObject({
      showThreshold: false,
      threshold: 17,
      showDedicatedFooter: false,
      timeFormat: '12-hour',
    })
  })

  it('normalizes one active spot into the versioned E1002 installation contract', () => {
    const configuration = createInstalledConfiguration({
      deviceTimezone: 'America/New_York',
      spot: {
        id: 'brouwersdam',
        name: 'Brouwersdam',
        latitude: 51.7506,
        longitude: 3.8577,
        timezone: 'Europe/Amsterdam',
      },
      modelId: 'best_match',
      display: {
        showThreshold: true,
        threshold: 23,
        showWeather: true,
        showTemperature: false,
        showTide: false,
        showDedicatedFooter: true,
        timeFormat: '24-hour',
        temperatureUnit: 'celsius',
      },
    })

    expect(configuration).toMatchObject({
      version: CONFIGURATION_VERSION,
      boardId: BOARD_ID,
      deviceTimezone: 'America/New_York',
      spot: { id: 'brouwersdam', name: 'Brouwersdam' },
      forecastModel: 'best_match',
    })
    expect(configuration.digest).toMatch(/^[0-9a-f]{16}$/)
    expect(validateInstalledConfiguration(configuration)).toBe(true)
    expect(installedConfigurationDigest(configuration)).toBe(configuration.digest)
    expect(JSON.stringify(configuration)).not.toContain('password')
  })

  it('matches the firmware default fixture digest', () => {
    const configuration = createInstalledConfiguration({
      spot: {
        id: 'brouwersdam', name: 'Brouwersdam', latitude: 51.7506, longitude: 3.8577,
        timezone: 'Europe/Amsterdam',
      },
      modelId: 'best_match',
      display: {
        showThreshold: false, threshold: 17, showWeather: true,
        showTemperature: false, showTide: false, timeFormat: '24-hour',
        showDedicatedFooter: false,
        temperatureUnit: 'celsius',
      },
    })
    expect(configuration.digest).toBe('1cb353796dfceaac')
  })

  it('changes the digest for a spot change but not for object key order', () => {
    const input = {
      spot: {
        id: 'edam', name: 'Edam', latitude: 52.5126, longitude: 5.0486,
        timezone: 'Europe/Amsterdam',
      },
      modelId: 'best_match',
      display: createDefaultDisplayConfiguration('nl-NL'),
    }
    const original = createInstalledConfiguration(input)
    const reordered = {
      digest: original.digest,
      display: { ...original.display },
      forecastModel: original.forecastModel,
      deviceTimezone: original.deviceTimezone,
      spot: { ...original.spot },
      boardId: original.boardId,
      version: original.version,
    }

    expect(installedConfigurationDigest(reordered)).toBe(original.digest)
    expect(createInstalledConfiguration({
      ...input,
      spot: { ...input.spot, id: 'brouwersdam', name: 'Brouwersdam' },
    }).digest).not.toBe(original.digest)
  })

  it.each([
    [{ latitude: -90, longitude: -180 }, true],
    [{ latitude: 90, longitude: 180 }, true],
    [{ latitude: -90.0001, longitude: 0 }, false],
    [{ latitude: 0, longitude: 180.0001 }, false],
  ])('validates coordinate bounds for %o', (coordinates, expected) => {
    const configuration = createInstalledConfiguration({
      spot: {
        id: 'bounds', name: 'Bounds', timezone: 'Etc/UTC', ...coordinates,
      },
      modelId: 'best_match',
      display: createDefaultDisplayConfiguration('nl-NL'),
    }, { allowInvalid: true })
    expect(validateInstalledConfiguration(configuration)).toBe(expected)
  })
})
