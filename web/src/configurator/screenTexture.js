import * as THREE from 'three'
import { brouwersdamForecast } from '../fixtures/brouwersdam'
import {
  loadSharedRenderer,
} from '../renderer/sharedRenderer'
import { RENDERER_DISPLAYS } from '../renderer/contract'
import { createRendererInput } from '../renderer/rendererInput'
import { BOARD_IDS } from '../config/configuration'

export { createRendererInput } from '../renderer/rendererInput'

const BOARD_RENDERER_DISPLAYS = Object.freeze({
  [BOARD_IDS.E1001]: RENDERER_DISPLAYS.E1001_GRAY4,
  [BOARD_IDS.E1002]: RENDERER_DISPLAYS.E1002_SPECTRA6,
  [BOARD_IDS.E1003]: RENDERER_DISPLAYS.E1003_GC16,
})

function grayscaleThresholdPreview(rgba, boardId, config) {
  if (!config?.showThreshold || boardId === BOARD_IDS.E1002) return rgba
  const preview = rgba.slice()
  for (let offset = 0; offset < preview.length; offset += 4) {
    const isRedThreshold = preview[offset] === 255 && preview[offset + 1] === 0 && preview[offset + 2] === 0
    const isE1003ThresholdGray = boardId === BOARD_IDS.E1003 &&
      preview[offset] === 85 && preview[offset + 1] === 85 && preview[offset + 2] === 85
    if (isRedThreshold || isE1003ThresholdGray) {
      preview[offset] = 0
      preview[offset + 1] = 0
      preview[offset + 2] = 0
    }
  }
  return preview
}

export async function createScreenTexture({
  forecast = brouwersdamForecast,
  config,
  boardId = BOARD_IDS.E1002,
  rendererLoader = loadSharedRenderer,
} = {}) {
  const renderer = await rendererLoader()
  let texture
  let currentForecast = forecast
  let currentConfig = config
  let disposed = false

  function renderFrame(nextForecast, nextConfig) {
    const input = createRendererInput(nextForecast, nextConfig)
    const frame = renderer.renderPreviewForDisplay(input, BOARD_RENDERER_DISPLAYS[boardId])
    const { data, width, height } = frame
    const rgba = grayscaleThresholdPreview(data, boardId, nextConfig)
    if (!(rgba instanceof Uint8Array) || rgba.byteLength !== width * height * 4) {
      throw new Error('The canonical renderer must return one complete 800 × 480 RGBA preview or its model-specific equivalent')
    }

    if (!texture) {
      texture = new THREE.DataTexture(
        rgba,
        width,
        height,
        THREE.RGBAFormat,
        THREE.UnsignedByteType,
      )
      texture.colorSpace = THREE.SRGBColorSpace
      texture.magFilter = THREE.LinearFilter
      texture.minFilter = THREE.LinearMipmapLinearFilter
      texture.generateMipmaps = true
      texture.flipY = true
    } else {
      texture.image.data = rgba
    }
    texture.needsUpdate = true
  }

  try {
    renderFrame(currentForecast, currentConfig)
  } catch (error) {
    renderer.dispose()
    throw error
  }

  return {
    texture,
    update({ forecast: nextForecast = currentForecast, config: nextConfig = currentConfig } = {}) {
      if (disposed) throw new Error('The screen texture has been disposed')
      renderFrame(nextForecast, nextConfig)
      currentForecast = nextForecast
      currentConfig = nextConfig
    },
    exportPng() {
      if (disposed) throw new Error('The screen texture has been disposed')
      const { data, width, height } = texture.image
      const canvas = document.createElement('canvas')
      canvas.width = width
      canvas.height = height
      canvas.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(data), width, height), 0, 0)
      return canvas.toDataURL('image/png')
    },
    dispose() {
      if (disposed) return
      disposed = true
      texture.dispose()
      renderer.dispose()
    },
  }
}
