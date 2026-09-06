import { describe, expect, it } from 'vitest'
import { HalfFloatType } from 'three'
import { createSceneComposer } from '../src/configurator/sceneComposer'

describe('scene edge rendering', () => {
  it.each([2, 4, 8])('keeps geometry antialiasing in both postprocessing buffers (GPU max %i)', (maxSamples) => {
    const composer = createSceneComposer({
      getPixelRatio: () => 2,
      getSize: (size) => size.set(390, 844),
      capabilities: { maxSamples },
    })
    try {
      for (const target of [composer.readBuffer, composer.writeBuffer]) {
        expect(target.samples).toBe(Math.min(4, maxSamples))
        expect(target.texture.type).toBe(HalfFloatType)
        expect(target.width).toBe(780)
      }
      composer.setSize(844, 390)
      composer.swapBuffers()
      expect(composer.readBuffer.samples).toBe(Math.min(4, maxSamples))
      expect(composer.readBuffer.width).toBe(1688)
      expect(composer.writeBuffer.height).toBe(780)
    } finally {
      composer.dispose()
    }
  })
})
