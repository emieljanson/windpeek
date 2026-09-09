import { readFile, readdir } from 'node:fs/promises'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import {
  RENDERER_CONTRACT_VERSION,
  RENDERER_PALETTE_BYTES,
  RENDERER_RGBA_BYTES,
  SharedRendererError,
  loadSharedRenderer,
} from '../src/renderer/sharedRenderer'

const webRoot = dirname(dirname(fileURLToPath(import.meta.url)))
const repositoryRoot = dirname(webRoot)
const wasmPath = join(webRoot, 'public', 'renderer', 'wind-renderer.wasm')
const fixtureDirectory = join(repositoryRoot, 'shared', 'renderer-fixtures')
const RENDERER_TEST_TIMEOUT_MS = 30_000

async function loadRealRenderer() {
  return loadSharedRenderer({ wasmBytes: await readFile(wasmPath) })
}

function fixtureInput(displayMode = 2, thresholdKt = 17, rowMask = 1, missingData = false) {
  const dayNames = ['TODAY', 'THURSDAY', 'FRIDAY', 'SATURDAY', 'SUNDAY']
  const dates = ['26 AUG', '27 AUG', '28 AUG', '29 AUG', '30 AUG']
  const times = ['08', '11', '14', '17', '20']
  return {
    version: RENDERER_CONTRACT_VERSION,
    spotName: 'Brouwersdam',
    provider: 'BEST MATCH',
    updatedTime: '26 AUG 11AM',
    state: 0,
    refreshFailed: false,
    ageHours: 1,
    batteryPercent: 74,
    displayMode,
    thresholdKt,
    use24Hour: false,
    temperatureFahrenheit: false,
    showWeather: Boolean(rowMask & 1),
    showTemperature: Boolean(rowMask & 2),
    showTide: Boolean(rowMask & 4),
    showDedicatedFooter: true,
    tideAvailable: Boolean(rowMask & 4) && !missingData,
    tideExtrema: [],
    tideSamples: (rowMask & 4) && !missingData
      ? Array.from({ length: 120 }, (_, index) => {
          const hour = index % 24
          const seaLevelMm = hour <= 6 ? hour * 100
            : hour <= 18 ? 600 - (hour - 6) * 100
              : -600 + (hour - 18) * 100
          return {
            dayIndex: Math.floor(index / 24),
            localHour: hour,
            seaLevelMm,
            available: true,
          }
        })
      : [],
    days: dayNames.map((day, dayIndex) => ({
      day,
      date: dates[dayIndex],
      samples: times.map((time, sampleIndex) => {
        const sustainedKt = 7 + dayIndex * 2 + sampleIndex * 3
        return {
          time,
          sustainedKt,
          gustKt: sustainedKt + 5,
          destinationDegrees: dayIndex * 55 + sampleIndex * 27,
          available: true,
          weather: missingData ? 0 : 1 + (dayIndex * 5 + sampleIndex) % 8,
          temperatureTenthsC: 120 + dayIndex * 5 + sampleIndex,
          temperatureAvailable: !missingData,
        }
      }),
    })),
  }
}

describe('shared WebAssembly renderer', { timeout: RENDERER_TEST_TIMEOUT_MS }, () => {
  it('gives high- and low-tide times balanced outer padding', async () => {
    const renderer = await loadRealRenderer()
    const input = fixtureInput(2, 17, 4)
    input.windSize = 0
    input.swellSize = 0
    input.tideExtrema = [
      { dayIndex: 0, localHour: 11, localMinute: 0, seaLevelMm: 600, isHigh: true, available: true },
      { dayIndex: 0, localHour: 17, localMinute: 0, seaLevelMm: -600, isHigh: false, available: true },
    ]
    const frame = renderer.renderPreviewForDisplay(input, 2)
    const inkRows = (top, bottom) => Array.from({ length: bottom - top }, (_, i) => top + i)
      .filter((y) => Array.from({ length: 130 }, (_, x) => frame.data[(y * 800 + x + 25) * 4]).some((value) => value < 128))
    const high = inkRows(370, 401)
    const low = inkRows(418, 449)
    expect(high.length).toBeGreaterThan(0)
    expect(low.length).toBeGreaterThan(0)
    expect(high[0] - 369).toBeGreaterThanOrEqual(10)
    expect(449 - low.at(-1)).toBeGreaterThanOrEqual(10)
    expect(Math.abs((high[0] - 369) - (449 - low.at(-1)))).toBeLessThanOrEqual(1)
  })

  it('balances the graph boundaries around direction and period, with five swell guides', async () => {
    const renderer = await loadRealRenderer()
    const input = fixtureInput(2, 17, 0)
    input.windSize = 0
    input.swellSize = 2
    input.days.forEach((day) => day.samples.forEach((sample) => {
      sample.swellHeightCm = -1
      sample.swellPeriodTenths = 90
      sample.swellDestinationDegrees = 90
    }))
    const frame = renderer.renderPreviewForDisplay(input, 2)
    const black = (x, y) => frame.data[(y * 800 + x) * 4] < 128
    const guides = Array.from({ length: 299 }, (_, i) => i + 114).filter((y) => black(13, y))
    expect(guides).toHaveLength(4)
    expect(guides[0]).toBe(149)
    expect(black(13, 417)).toBe(true)
    const allGuides = [...guides, 417]
    const gaps = allGuides.slice(1).map((y, index) => y - allGuides[index])
    expect(gaps.every((gap, index) => index === 0 || gap > gaps[index - 1])).toBe(true)
    const inkRows = (top, bottom) => Array.from({ length: bottom - top }, (_, i) => top + i)
      .filter((y) => Array.from({ length: 22 }, (_, x) => 27 + x).some((x) => black(x, y)))
    const icon = inkRows(114, 149)
    const period = inkRows(418, 449)
    expect(icon.length).toBeGreaterThan(0)
    expect(period.length).toBeGreaterThan(0)
    expect(Math.abs((icon[0] - 113) - (149 - icon.at(-1)))).toBeLessThanOrEqual(1)
    expect(Math.abs((period[0] - 417) - (449 - period.at(-1)))).toBeLessThanOrEqual(1)
    input.windSize = 2
    input.swellSize = 0
    const wind = renderer.renderPreviewForDisplay(input, 2)
    expect(wind.data[(149 * 800 + 13) * 4]).toBe(0)
  })

  it('aligns the last text row of compact wind and swell to the same grid', async () => {
    const renderer = await loadRealRenderer()
    const input = fixtureInput(2, 17, 0)
    input.windSize = 1
    input.swellSize = 1
    input.days.forEach((day) => day.samples.forEach((sample) => {
      sample.gustKt = 9
      sample.swellHeightCm = 90
      sample.swellPeriodTenths = 90
      sample.swellDestinationDegrees = 90
    }))
    const frame = renderer.renderPreviewForDisplay(input, 2)
    const inkRows = (top) => Array.from({ length: 20 }, (_, y) => y).filter((y) =>
      Array.from({ length: 22 }, (_, x) => frame.data[((top + y) * 800 + 27 + x) * 4]).some((value) => value < 128))
    const wind = inkRows(169)
    const swell = inkRows(169 + 84)
    expect(wind.length).toBeGreaterThan(0)
    expect(swell).toEqual(wind)
  })

  it('uses hourly heights to reach the day edge and leaves missing hours unconnected', async () => {
    const renderer = await loadRealRenderer()
    const input = fixtureInput(2, 17, 0)
    input.windSize = 0
    input.swellSize = 2
    input.swellHourly = Array.from({ length: 120 }, (_, index) => ({
      dayIndex: Math.floor(index / 24), hour: index % 24,
      heightCm: index % 24 === 14 ? -1 : 80,
    }))
    input.days.forEach((day) => day.samples.forEach((sample, index) => {
      sample.swellHeightCm = index === 2 ? -1 : 80
      sample.swellPeriodTenths = 90
      sample.swellDestinationDegrees = 90
    }))
    const frame = renderer.renderPreviewForDisplay(input, 2)
    const pixel = (x, y) => frame.data[(y * frame.width + x) * 4]
    const y = 417 - Math.round(Math.log1p(80 / 50) / Math.log1p(1000 / 50) * (417 - 149))
    expect(pixel(14, y)).toBeLessThan(128)
    expect(pixel(33, y)).toBeLessThan(128)
    expect(pixel(34, y)).toBe(255)
    expect(pixel(36, y)).toBe(0)
    expect(pixel(90, y)).toBe(255)
    expect(pixel(90, y + 1)).toBe(255)
    // A higher forecast elsewhere must not move the first day's curve.
    input.swellHourly.filter((sample) => sample.dayIndex === 4).forEach((sample) => { sample.heightCm = 1500 })
    const high = renderer.renderPreviewForDisplay(input, 2)
    expect(high.data[(y * frame.width + 14) * 4]).toBe(frame.data[(y * frame.width + 14) * 4])
  })

  it('keeps the primary stroke continuous over secondary dashes and clears secondary data between renders', async () => {
    const renderer = await loadRealRenderer()
    const input = { ...fixtureInput(2, 17, 0), windSize: 0, swellSize: 2, moduleOrder: [0, 1, 2, 3, 4] }
    input.swellHourly = Array.from({ length: 120 }, (_, i) => ({ dayIndex: Math.floor(i / 24), hour: i % 24, heightCm: 80, secondaryHeightCm: 80 }))
    input.days.forEach(day => day.samples.forEach(sample => Object.assign(sample, { swellHeightCm: 80, swellPeriodTenths: 90, swellDestinationDegrees: 270, secondarySwellHeightCm: 80, secondarySwellPeriodTenths: 60, secondarySwellDestinationDegrees: 90 })))
    const overlap = renderer.renderPreviewForDisplay(input, 2)
    const y = Math.round(417 - Math.log1p(80 / 50) / Math.log1p(1000 / 50) * (417 - 149))
    // Away from labels/markers: the primary line stays solid and has a white halo.
    for (let x = 17; x < 25; x++) {
      expect(overlap.data[(y * 800 + x) * 4]).toBeLessThan(128)
      expect(overlap.data[((y - 2) * 800 + x) * 4]).toBe(255)
    }
    input.swellHourly.forEach(sample => { sample.secondaryHeightCm = 30 })
    const separated = renderer.renderPreviewForDisplay(input, 2)
    expect(separated.data).not.toEqual(overlap.data)
    input.swellHourly.forEach(sample => { delete sample.secondaryHeightCm })
    input.days.forEach(day => day.samples.forEach(sample => {
      delete sample.secondarySwellHeightCm; delete sample.secondarySwellPeriodTenths; delete sample.secondarySwellDestinationDegrees
    }))
    const reset = renderer.renderPreviewForDisplay(input, 2)
    const fresh = await loadRealRenderer()
    expect(reset.data).toEqual(fresh.renderPreviewForDisplay(input, 2).data)
  })

  it('renders explicit order independently of size and rejects malformed orders', async () => {
    const renderer = await loadRealRenderer()
    const input = { ...fixtureInput(2, 17, 7), windSize: 1, swellSize: 2, moduleOrder: [1, 0, 2, 3, 4] }
    input.days.forEach(day => day.samples.forEach(sample => Object.assign(sample, { swellHeightCm: 80, swellPeriodTenths: 90, swellDestinationDegrees: 270 })))
    const original = renderer.render(input)
    input.moduleOrder = [4, 2, 3, 0, 1]
    expect(renderer.render(input)).not.toEqual(original)
    input.moduleOrder = [1, 0, 2, 3, 4]
    expect(renderer.render(input)).toEqual(original)
    input.moduleOrder = [1, 0, 2, 3, 3]
    expect(() => renderer.render(input)).toThrow()
  })

  it('renders swell with all optional rows and resets cleanly to wind', async () => {
    const renderer = await loadRealRenderer()
    const wind = renderer.render(fixtureInput())
    for (let rowMask = 0; rowMask < 8; rowMask++) {
      const input = fixtureInput(2, 17, rowMask)
      input.swellFocus = true
      input.swellHourly = Array.from({ length: 120 }, (_, index) => ({
        dayIndex: Math.floor(index / 24), hour: index % 24,
        heightCm: index % 24 === 14 ? -1 : 80,
      }))
      input.days.forEach((day) => day.samples.forEach((sample, index) => {
        sample.swellHeightCm = index === 2 ? -1 : index * 40
        sample.swellPeriodTenths = index === 2 ? -1 : 95
        sample.swellDestinationDegrees = index === 2 ? -1 : 270
      }))
      for (const [windSize, swellSize] of [[0, 0], [0, 1], [0, 2], [1, 0], [1, 1], [1, 2], [2, 1], [2, 2]]) {
        input.windSize = windSize
        input.swellSize = swellSize
        input.swellFocus = swellSize > 0
        for (const displayId of [1, 2, 3]) {
          const frame = renderer.renderPreviewForDisplay(input, displayId)
          expect(frame.width).toBe(800)
          expect(frame.height).toBe(displayId === 3 ? 600 : 480)
          expect(frame.data).toHaveLength(frame.width * frame.height * 4)
        }
      }
    }
    expect(renderer.render(fixtureInput())).toEqual(wind)
  })

  it('matches every full native palette fixture byte for byte', async () => {
    const renderer = await loadRealRenderer()
    const fixtureNames = (await readdir(fixtureDirectory)).filter((name) => name.endsWith('.bin'))
    const fixtures = [
      ['threshold-05.bin', fixtureInput(1, 5)],
      ['threshold-17.bin', fixtureInput(1, 17)],
      ['threshold-35.bin', fixtureInput(1, 35)],
      ['solid-17.bin', fixtureInput(2, 17)],
      ...Array.from({ length: 8 }, (_, rowMask) => [
        `rows-${Boolean(rowMask & 1) ? 1 : 0}${Boolean(rowMask & 2) ? 1 : 0}${Boolean(rowMask & 4) ? 1 : 0}.bin`,
        fixtureInput(2, 17, rowMask),
      ]),
      ['rows-111-missing.bin', fixtureInput(2, 17, 7, true)],
    ]

    expect(fixtureNames.sort()).toEqual([
      'rows-000.bin',
      'rows-001.bin',
      'rows-010.bin',
      'rows-011.bin',
      'rows-100.bin',
      'rows-101.bin',
      'rows-110.bin',
      'rows-111-missing.bin',
      'rows-111.bin',
      'solid-17.bin',
      'threshold-05.bin',
      'threshold-17.bin',
      'threshold-35.bin',
    ])
    expect(renderer.width).toBe(800)
    expect(renderer.height).toBe(480)
    expect(renderer.paletteBytes).toBe(RENDERER_PALETTE_BYTES)

    for (const [fixtureName, input] of fixtures) {
      const expected = new Uint8Array(await readFile(join(fixtureDirectory, fixtureName)))
      const actual = renderer.render(input)
      expect(actual).toHaveLength(RENDERER_PALETTE_BYTES)
      expect(actual, fixtureName).toEqual(expected)
    }
  })

  it('preserves red threshold pixels and output across repeated renders', async () => {
    const renderer = await loadRealRenderer()
    const first = renderer.render(fixtureInput(1, 35))
    renderer.renderPreview(fixtureInput(1, 35))
    const second = renderer.render(fixtureInput(1, 35))

    expect(first).toEqual(second)
    expect(first.filter((value) => value === 3).length).toBeGreaterThan(0)
  })

  it('returns a clean grayscale preview with red accents from the same renderer', async () => {
    const renderer = await loadRealRenderer()
    const solid = renderer.renderPreview(fixtureInput(2, 17))
    const threshold = renderer.renderPreview(fixtureInput(1, 17))

    expect(solid).toHaveLength(RENDERER_RGBA_BYTES)
    let hasContinuousGray = false
    let hasRed = false
    let allAlphaOpaque = true
    for (let offset = 0; offset < solid.length; offset += 4) {
      allAlphaOpaque &&= solid[offset + 3] === 255
      if (solid[offset] === solid[offset + 1] &&
          solid[offset + 1] === solid[offset + 2] &&
          solid[offset] > 0 && solid[offset] < 255) hasContinuousGray = true
      if (threshold[offset] === 255 && threshold[offset + 1] === 0 &&
          threshold[offset + 2] === 0 && threshold[offset + 3] === 255) hasRed = true
    }
    expect(allAlphaOpaque).toBe(true)
    expect(hasContinuousGray).toBe(true)
    expect(hasRed).toBe(true)
  })

  it('renders the E1003 preview as a native 4:3 grayscale composition', async () => {
    const renderer = await loadRealRenderer()
    const frame = renderer.renderPreviewForDisplay(fixtureInput(1, 17), 3)

    expect(frame.width).toBe(800)
    expect(frame.height).toBe(600)
    expect(frame.data).toHaveLength(800 * 600 * 4)
    for (let offset = 0; offset < frame.data.length; offset += 4) {
      expect(frame.data[offset]).toBe(frame.data[offset + 1])
      expect(frame.data[offset + 1]).toBe(frame.data[offset + 2])
      expect(frame.data[offset + 3]).toBe(255)
    }
  })

  it('crosses the flat setter bridge without depending on native struct layout', async () => {
    const renderer = await loadRealRenderer()
    const expected = new Uint8Array(await readFile(join(fixtureDirectory, 'solid-17.bin')))

    const first = renderer.render(fixtureInput())
    const second = renderer.render(fixtureInput())

    expect(first).toEqual(expected)
    expect(second).toEqual(expected)
  })

  it('passes temperature and tide through the shared bridge', async () => {
    const renderer = await loadRealRenderer()
    const input = fixtureInput()
    input.showTemperature = true
    input.showTide = true
    input.tideAvailable = true
    input.tideSamples = Array.from({ length: 120 }, (_, index) => ({
      dayIndex: Math.floor(index / 24),
      localHour: index % 24,
      seaLevelMm: Math.round(Math.sin(index / 6) * 800),
      available: true,
    }))
    input.tideExtrema = [{
      dayIndex: 0,
      localHour: 14,
      localMinute: 15,
      seaLevelMm: 800,
      isHigh: true,
      available: true,
    }]

    const first = renderer.renderPreview(input)
    const second = renderer.renderPreview(input)

    expect(first).toHaveLength(RENDERER_RGBA_BYTES)
    expect(second).toEqual(first)
  })

  it('renders clock and temperature-unit preferences through the shared bridge', async () => {
    const renderer = await loadRealRenderer()
    const metric = fixtureInput(2, 17, 7)
    const imperial = structuredClone(metric)
    imperial.use24Hour = true
    imperial.temperatureFahrenheit = true

    expect(renderer.renderPreview(imperial)).not.toEqual(renderer.renderPreview(metric))
  })

  it('rejects an incompatible contract before returning any bitmap', async () => {
    const renderer = await loadRealRenderer()

    expect(() => renderer.render({ version: RENDERER_CONTRACT_VERSION + 1 })).toThrowError(
      expect.objectContaining({ code: 'INCOMPATIBLE_CONTRACT' }),
    )
  })

  it('rejects values outside the bounded string bridge', async () => {
    const renderer = await loadRealRenderer()
    const input = fixtureInput()
    input.spotName = 'x'.repeat(96)

    expect(() => renderer.render(input)).toThrowError(
      expect.objectContaining({ code: 'INVALID_INPUT' }),
    )
  })

  it('turns a missing module into a controlled renderer-load error', async () => {
    const fetchImpl = async () => ({ ok: false, status: 404 })
    const attempt = loadSharedRenderer({ wasmUrl: '/missing-renderer.wasm', fetchImpl })

    await expect(attempt).rejects.toBeInstanceOf(SharedRendererError)
    await expect(attempt).rejects.toMatchObject({ code: 'LOAD_FAILED' })
  })

  it('bounds a renderer request that never completes', async () => {
    const fetchImpl = (_url, { signal }) => new Promise((_resolve, reject) => {
      signal.addEventListener('abort', () => reject(new Error('aborted')), { once: true })
    })

    await expect(loadSharedRenderer({ fetchImpl, timeoutMs: 1 })).rejects.toMatchObject({
      code: 'LOAD_TIMEOUT',
    })
  })

  it('rejects an incompatible renderer module during loading', async () => {
    const bytes = await readFile(wasmPath)
    const compiled = await WebAssembly.instantiate(bytes, {})
    const incompatibleExports = {
      ...compiled.instance.exports,
      wind_wasm_contract_version: () => RENDERER_CONTRACT_VERSION + 1,
    }

    await expect(loadSharedRenderer({
      wasmBytes: bytes,
      instantiate: async () => ({ instance: { exports: incompatibleExports } }),
    })).rejects.toMatchObject({ code: 'INCOMPATIBLE_RENDERER' })
  })
})
