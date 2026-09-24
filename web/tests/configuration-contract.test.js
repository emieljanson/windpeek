import { describe, expect, it } from 'vitest'
import Ajv2020 from 'ajv/dist/2020.js'
import schema from '../../contracts/windpeek-config.schema.json'
import fixture from '../../shared/configuration-fixtures/e1003-ten-spots.json'
import {
  BOARD_IDS,
  CONFIGURATION_VERSION,
  MULTI_CONFIGURATION_VERSION,
  SUPPORTED_BOARD_IDS,
  TIME_FORMATS,
  TEMPERATURE_UNITS,
  installedConfigurationDigest,
  validateInstalledConfiguration,
} from '../src/config/configuration'

const validatesSchema = new Ajv2020().compile(schema)

describe('shared installation contract', () => {
  it('keeps the web constants and schema aligned', () => {
    expect(schema.properties.version.enum).toEqual([CONFIGURATION_VERSION, MULTI_CONFIGURATION_VERSION])
    expect(schema.properties.boardId.enum).toEqual(SUPPORTED_BOARD_IDS)
    expect(schema.properties.display.properties.timeFormat.enum).toEqual(TIME_FORMATS)
    expect(schema.properties.display.properties.temperatureUnit.enum).toEqual(TEMPERATURE_UNITS)
    expect(schema.allOf[0].then.properties.boardId.const).toBe(BOARD_IDS.E1003)
  })

  it.each([fixture.additionalSpots[0], fixture])('accepts a shared v$version fixture in both validators', configuration => {
    expect(validatesSchema(configuration)).toBe(true)
    expect(validateInstalledConfiguration(configuration)).toBe(true)
    expect(installedConfigurationDigest(configuration)).toBe(configuration.digest)
  })

  it.each([
    ['unknown display field', config => { config.display.extra = true }],
    ['missing display field', config => { delete config.display.timeFormat }],
    ['unknown root field', config => { config.extra = true }],
    ['missing digest', config => { delete config.digest }],
    ['invalid time format', config => { config.display.timeFormat = 'local' }],
    ['duplicate module', config => { config.display.moduleOrder[1] = 'wind' }],
  ])('rejects %s in both validators', (_, change) => {
    const configuration = structuredClone(fixture.additionalSpots[0])
    change(configuration)
    expect(validatesSchema(configuration)).toBe(false)
    expect(validateInstalledConfiguration(configuration)).toBe(false)
  })
})
