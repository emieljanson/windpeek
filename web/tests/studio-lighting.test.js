import { describe, expect, it, vi } from 'vitest'
import * as THREE from 'three'
import { BOARD_IDS } from '../src/config/configuration'
import { PRODUCT_LIGHTING, DARK_PRODUCT_LIGHTING } from '../src/configurator/productLighting'
import { createStudioLighting } from '../src/configurator/studioLighting'

vi.mock('../src/configurator/studioEnvironment', () => ({
  createProductStudioEnvironment: vi.fn(() => ({ texture: {}, dispose: vi.fn() })),
}))

describe('studio lighting', () => {
  it('returns to the same light values after switching themes', () => {
    const scene = new THREE.Scene()
    const renderer = { toneMappingExposure: 1 }
    const studio = createStudioLighting(scene, renderer, BOARD_IDS.E1003)
    studio.apply({ dark: true, captureMode: false })
    expect(studio.keyLight.intensity).toBe(DARK_PRODUCT_LIGHTING.key.intensity * 1.4)
    expect(renderer.toneMappingExposure).toBe(1.4)
    studio.apply({ dark: false, captureMode: false })
    expect(studio.keyLight.intensity).toBe(PRODUCT_LIGHTING.key.intensity)
    expect(renderer.toneMappingExposure).toBe(1)
    expect(scene.fog).toBeNull()
  })
})
