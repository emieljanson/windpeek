import { describe, expect, it, vi } from 'vitest'
import { createRendererInput, createScreenTexture } from '../src/configurator/screenTexture'
import { brouwersdamForecast } from '../src/fixtures/brouwersdam'
import { DISPLAY_MODES, RENDERER_DISPLAYS } from '../src/renderer/contract'
import { BOARD_IDS } from '../src/config/configuration'
import { RENDERER_CONTRACT_VERSION, RENDERER_RGBA_BYTES } from '../src/renderer/sharedRenderer'

function completeFrame(red = 255, green = 255, blue = 255) {
  const frame = new Uint8Array(RENDERER_RGBA_BYTES)
  for (let offset = 0; offset < frame.length; offset += 4) {
    frame[offset] = red
    frame[offset + 1] = green
    frame[offset + 2] = blue
    frame[offset + 3] = 255
  }
  return frame
}

function fakeRenderer(frames) {
  const renderPreview = vi.fn(() => frames.shift())
  return {
    renderPreview,
    renderPreviewForDisplay: vi.fn((input) => ({
      data: renderPreview(input),
      width: 800,
      height: 480,
    })),
    dispose: vi.fn(),
  }
}

describe('canonical screen texture', () => {
  it('builds the initial texture only from the shared renderer output', async () => {
    const renderer = fakeRenderer([completeFrame(255, 0, 0)])
    const rendererLoader = vi.fn(async () => renderer)

    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: false, threshold: 17 },
      rendererLoader,
    })

    expect(rendererLoader).toHaveBeenCalledOnce()
    expect(renderer.renderPreview).toHaveBeenCalledOnce()
    expect(renderer.renderPreview.mock.calls[0][0]).toMatchObject({
      version: RENDERER_CONTRACT_VERSION,
      spotName: 'Brouwersdam',
      provider: 'BEST MATCH',
      batteryPercent: 70,
      displayMode: DISPLAY_MODES.solid,
      thresholdKt: 17,
      days: expect.arrayContaining([
        expect.objectContaining({
          samples: expect.arrayContaining([
            expect.objectContaining({ sustainedKt: 8, gustKt: 13 }),
          ]),
        }),
      ]),
    })
    expect([...source.texture.image.data.slice(0, 4)]).toEqual([255, 0, 0, 255])
  })

  it('rerenders cached forecast input for settings updates without loading again', async () => {
    const renderer = fakeRenderer([
      completeFrame(),
      completeFrame(0, 0, 0),
      completeFrame(255, 0, 0),
    ])
    const rendererLoader = vi.fn(async () => renderer)
    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: false, threshold: 17 },
      rendererLoader,
    })
    const firstFrame = source.texture.image.data

    source.update({ config: { showThreshold: true, treatment: 'solid', threshold: 25 } })
    const secondFrame = source.texture.image.data
    source.update({ config: { showThreshold: false, treatment: 'threshold-line', threshold: 30 } })

    expect(rendererLoader).toHaveBeenCalledOnce()
    expect(renderer.renderPreview).toHaveBeenCalledTimes(3)
    expect(renderer.renderPreview.mock.calls.map(([input]) => [input.displayMode, input.thresholdKt])).toEqual([
      [DISPLAY_MODES.solid, 17],
      [DISPLAY_MODES['threshold-line'], 25],
      [DISPLAY_MODES.solid, 30],
    ])
    expect([...source.texture.image.data.slice(0, 4)]).toEqual([255, 0, 0, 255])
    expect(secondFrame).not.toBe(firstFrame)
    expect(source.texture.image.data).not.toBe(secondFrame)
  })

  it.each([
    [BOARD_IDS.E1001, RENDERER_DISPLAYS.E1001_GRAY4],
    [BOARD_IDS.E1003, RENDERER_DISPLAYS.E1003_GC16],
  ])('renders a black threshold on grayscale model %s', async (boardId, display) => {
    const renderer = fakeRenderer([completeFrame(255, 0, 0)])
    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: true, threshold: 17 },
      boardId,
      rendererLoader: async () => renderer,
    })

    expect(renderer.renderPreviewForDisplay).toHaveBeenCalledWith(expect.anything(), display)
    expect([...source.texture.image.data.slice(0, 4)]).toEqual([0, 0, 0, 255])
  })

  it('keeps the threshold red on the E1002 colour display', async () => {
    const renderer = fakeRenderer([completeFrame(255, 0, 0)])
    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: true, threshold: 17 },
      boardId: BOARD_IDS.E1002,
      rendererLoader: async () => renderer,
    })

    expect([...source.texture.image.data.slice(0, 4)]).toEqual([255, 0, 0, 255])
  })

  it('turns the native E1003 threshold gray into preview black', async () => {
    const renderer = fakeRenderer([completeFrame(85, 85, 85)])
    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: true, threshold: 17 },
      boardId: BOARD_IDS.E1003,
      rendererLoader: async () => renderer,
    })

    expect([...source.texture.image.data.slice(0, 4)]).toEqual([0, 0, 0, 255])
  })

  it('maps row choices and five local tide days into renderer input', () => {
    const tide = {
      capability: 'available',
      samples: brouwersdamForecast.days.flatMap((day, dayIndex) =>
        Array.from({ length: 24 }, (_, hour) => ({
          localDate: day.localDate,
          localTime: `${String(hour).padStart(2, '0')}:00`,
          seaLevelMm: dayIndex * 100 + hour,
        }))),
      extrema: [{
        localDate: brouwersdamForecast.days[1].localDate,
        localTime: '14:15',
        seaLevelMm: 725,
        type: 'high',
      }],
    }

    const input = createRendererInput(brouwersdamForecast, {
      showThreshold: false,
      threshold: 17,
      showWeather: false,
      showTemperature: true,
      showTide: true,
      timeFormat: '12-hour',
      temperatureUnit: 'fahrenheit',
      tide,
    })

    expect(input).toMatchObject({
      showWeather: false,
      showTemperature: true,
      showTide: true,
      showDedicatedFooter: false,
      tideAvailable: true,
      use24Hour: false,
      temperatureFahrenheit: true,
    })
    expect(input.tideSamples).toHaveLength(120)
    expect(input.tideSamples[24]).toMatchObject({ dayIndex: 1, localHour: 0 })
    expect(input.tideExtrema).toEqual([{
      dayIndex: 1,
      localHour: 14,
      localMinute: 15,
      seaLevelMm: 725,
      isHigh: true,
      available: true,
    }])
  })

  it('formats sample labels for the selected clock mode before they cross the renderer bridge', () => {
    const twentyFourHour = createRendererInput(brouwersdamForecast, {
      showThreshold: false, threshold: 17, timeFormat: '24-hour', temperatureUnit: 'celsius',
    })
    const twelveHour = createRendererInput(brouwersdamForecast, {
      showThreshold: false, threshold: 17, timeFormat: '12-hour', temperatureUnit: 'celsius',
    })

    expect(twentyFourHour.days[0].samples.map((sample) => sample.time))
      .toEqual(['08', '11', '14', '17', '20'])
    expect(twelveHour.days[0].samples.map((sample) => sample.time))
      .toEqual(['8AM', '11AM', '2PM', '5PM', '8PM'])
  })

  it('formats the update time in the device timezone using the selected clock', () => {
    const forecast = {
      ...brouwersdamForecast,
      deviceTimezone: 'America/New_York',
    }
    const twentyFourHour = createRendererInput(forecast, {
      showThreshold: false, threshold: 17, timeFormat: '24-hour', temperatureUnit: 'celsius',
    })
    const twelveHour = createRendererInput(forecast, {
      showThreshold: false, threshold: 17, timeFormat: '12-hour', temperatureUnit: 'celsius',
    })

    expect(twentyFourHour.updatedTime).toBe('26 AUG 01:40')
    expect(twelveHour.updatedTime).toBe('26 AUG 1AM')
    expect(twentyFourHour.days[0].samples.map((sample) => sample.time))
      .toEqual(['08', '11', '14', '17', '20'])
  })

  it('keeps explicit battery data instead of the web demo fallback', () => {
    const input = createRendererInput({ ...brouwersdamForecast, batteryPercent: 42 }, {
      showThreshold: false, threshold: 17, timeFormat: '24-hour', temperatureUnit: 'celsius',
    })

    expect(input.batteryPercent).toBe(42)
  })

  it('does not publish an incomplete frame during rapid updates', async () => {
    const initial = completeFrame(0, 0, 0)
    const renderer = fakeRenderer([initial, new Uint8Array(24), completeFrame(255, 0, 0)])
    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: false, threshold: 17 },
      rendererLoader: async () => renderer,
    })

    expect(() => source.update({ config: { showThreshold: true, threshold: 18 } })).toThrow(
      /complete 800 × 480 RGBA preview/,
    )
    expect([...source.texture.image.data.slice(0, 4)]).toEqual([0, 0, 0, 255])

    source.update({ config: { showThreshold: true, threshold: 19 } })
    expect([...source.texture.image.data.slice(0, 4)]).toEqual([255, 0, 0, 255])
  })

  it('renders a changed spot forecast before publishing its new frame', async () => {
    const renderer = fakeRenderer([
      completeFrame(0, 0, 0),
      completeFrame(255, 0, 0),
    ])
    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: false, threshold: 17 },
      rendererLoader: async () => renderer,
    })
    const edam = structuredClone(brouwersdamForecast)
    edam.spotId = 'edam'
    edam.spotName = 'EDAM'
    edam.days[0].samples[0].sustainedKt = 24

    source.update({ forecast: edam })

    const renderedInput = renderer.renderPreview.mock.calls[1][0]
    expect(renderedInput.spotName).toBe('EDAM')
    expect(renderedInput.days[0].samples[0].sustainedKt).toBe(24)
    expect([...source.texture.image.data.slice(0, 4)]).toEqual([255, 0, 0, 255])
  })

  it('releases the shared renderer and Three.js texture together', async () => {
    const renderer = fakeRenderer([completeFrame()])
    const source = await createScreenTexture({
      forecast: brouwersdamForecast,
      config: { showThreshold: false, threshold: 17 },
      rendererLoader: async () => renderer,
    })
    const disposed = vi.fn()
    source.texture.addEventListener('dispose', disposed)

    source.dispose()
    source.dispose()

    expect(renderer.dispose).toHaveBeenCalledOnce()
    expect(disposed).toHaveBeenCalledOnce()
  })

  it('keeps supported renderer mode numbers stable at the shared ABI boundary', () => {
    expect(DISPLAY_MODES).toEqual({
      'threshold-line': 1,
      solid: 2,
    })
  })
})
