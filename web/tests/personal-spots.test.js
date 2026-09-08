import { describe, expect, it } from 'vitest'

import {
  createPersonalSpot,
  readPersonalSpots,
  writePersonalSpot,
} from '../src/spots/personalSpots'

function memoryStorage() {
  const values = new Map()
  return {
    getItem: (key) => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
  }
}

describe('personal spots', () => {
  it('creates a stable, renderer-safe spot from a confirmed map pin', () => {
    expect(createPersonalSpot({
      name: 'Edam harbour',
      latitude: 52.50673,
      longitude: 5.07729,
      timezone: 'Europe/Amsterdam',
      countryCode: 'nl',
      providerRef: 'geoapify:edam-id',
    })).toEqual({
      id: 'personal-edam-harbour-8hfw1-b0onl',
      name: 'Edam harbour',
      displayName: 'EDAM HARBOUR',
      latitude: 52.50673,
      longitude: 5.07729,
      timezone: 'Europe/Amsterdam',
      countryCode: 'nl',
      providerRef: 'geoapify:edam-id',
      personal: true,
    })
  })

  it('persists valid spots locally, updates duplicates, and ignores corrupt data', () => {
    const storage = memoryStorage()
    const spot = createPersonalSpot({
      name: 'Edam harbour', latitude: 52.50673, longitude: 5.07729,
      timezone: 'Europe/Amsterdam', providerRef: 'geoapify:edam-id',
    })
    expect(writePersonalSpot(spot, storage)).toBe(true)
    expect(writePersonalSpot({ ...spot, name: 'Edam water' }, storage)).toBe(true)
    expect(readPersonalSpots(storage)).toEqual([{ ...spot, name: 'Edam water' }])

    storage.setItem('windpeek.personal-spots', '{"version":1,"spots":[{"id":"bad"}]}')
    expect(readPersonalSpots(storage)).toEqual([])
  })

  it('rejects invalid coordinates and timezones', () => {
    expect(() => createPersonalSpot({
      name: 'Nowhere', latitude: 200, longitude: 5, timezone: 'Europe/Amsterdam',
    })).toThrow('valid coordinates')
    expect(() => createPersonalSpot({
      name: 'Nowhere', latitude: 52, longitude: 5, timezone: 'Not/AZone',
    })).toThrow('valid timezone')
  })
})
