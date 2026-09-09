export const RENDERER_CONTRACT_VERSION = 10
export const RENDERER_WIDTH = 800
export const RENDERER_HEIGHT = 480
export const RENDERER_PALETTE_BYTES = RENDERER_WIDTH * RENDERER_HEIGHT
export const RENDERER_RGBA_BYTES = RENDERER_PALETTE_BYTES * 4
export const RENDERER_DISPLAYS = Object.freeze({
  E1001_GRAY4: 1,
  E1002_SPECTRA6: 2,
  E1003_GC16: 3,
})
export const RENDERER_PREVIEW_DIMENSIONS = Object.freeze({
  [RENDERER_DISPLAYS.E1001_GRAY4]: Object.freeze({ width: 800, height: 480 }),
  [RENDERER_DISPLAYS.E1002_SPECTRA6]: Object.freeze({ width: 800, height: 480 }),
  [RENDERER_DISPLAYS.E1003_GC16]: Object.freeze({ width: 800, height: 600 }),
})

export const DISPLAY_MODES = Object.freeze({
  'threshold-line': 1,
  solid: 2,
})

export const MIN_THRESHOLD = 5
export const DEFAULT_THRESHOLD = 17
export const MAX_THRESHOLD = 35

export const RENDERER_TEXT_CAPACITIES = Object.freeze({
  spotName: 96,
  provider: 32,
  updatedTime: 32,
  day: 16,
  date: 16,
  time: 8,
})

const encoder = new TextEncoder()

export function textFitsRenderer(value, capacity) {
  return typeof value === 'string' && !value.includes('\0') && encoder.encode(value).byteLength < capacity
}
