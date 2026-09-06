import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia } from 'pinia'
import { useConfiguratorStore } from '../src/stores/configurator'
import { loadSharedRenderer } from '../src/renderer/sharedRenderer'

const { frame, projectiveScreen } = vi.hoisted(() => ({
  frame: { data: new Uint8Array(16), width: 2, height: 2 },
  projectiveScreen: {
    setFrame: vi.fn(),
    draw: vi.fn(),
    dispose: vi.fn(),
  },
}))

vi.mock('../src/renderer/sharedRenderer', () => ({
  loadSharedRenderer: vi.fn().mockResolvedValue({
    renderPreviewForDisplay: vi.fn(() => frame),
    dispose: vi.fn(),
  }),
}))

vi.mock('../src/marketing/projectiveScreen', () => ({
  createProjectiveScreen: vi.fn(() => projectiveScreen),
}))

import LandingHero from '../src/components/LandingHero.vue'

describe('landing hero nearby default', () => {
  beforeEach(() => {
    projectiveScreen.setFrame.mockClear()
    projectiveScreen.draw.mockClear()
    projectiveScreen.dispose.mockClear()
  })

  it('starts forecast and tide before the fire-and-forget nearby lookup', async () => {
    const pinia = createPinia()
    const store = useConfiguratorStore(pinia)
    const initializeForecast = vi.spyOn(store, 'initializeForecast').mockResolvedValue(false)
    const initializeTide = vi.spyOn(store, 'initializeTide').mockResolvedValue(false)
    const initializeNearbyDefault = vi.spyOn(store, 'initializeNearbyDefault').mockResolvedValue(false)

    const wrapper = mount(LandingHero, { global: { plugins: [pinia] } })
    await vi.waitFor(() => expect(initializeNearbyDefault).toHaveBeenCalledOnce())

    expect(initializeForecast).toHaveBeenCalledOnce()
    expect(initializeTide).toHaveBeenCalledOnce()
    expect(initializeForecast.mock.invocationCallOrder[0]).toBeLessThan(
      initializeNearbyDefault.mock.invocationCallOrder[0],
    )
    expect(initializeTide.mock.invocationCallOrder[0]).toBeLessThan(
      initializeNearbyDefault.mock.invocationCallOrder[0],
    )
    await vi.waitFor(() => expect(projectiveScreen.setFrame).toHaveBeenCalledWith(frame))
    wrapper.unmount()
  })

  it('starts the nearby lookup and releases WebGL when the renderer cannot load', async () => {
    loadSharedRenderer.mockRejectedValueOnce(new Error('renderer unavailable'))

    const pinia = createPinia()
    const store = useConfiguratorStore(pinia)
    vi.spyOn(store, 'initializeForecast').mockResolvedValue(false)
    vi.spyOn(store, 'initializeTide').mockResolvedValue(false)
    const initializeNearbyDefault = vi.spyOn(store, 'initializeNearbyDefault').mockResolvedValue(false)

    const wrapper = mount(LandingHero, { global: { plugins: [pinia] } })
    await vi.waitFor(() => expect(initializeNearbyDefault).toHaveBeenCalledOnce())
    await vi.waitFor(() => expect(projectiveScreen.dispose).toHaveBeenCalledOnce())

    wrapper.unmount()
    expect(projectiveScreen.dispose).toHaveBeenCalledOnce()
  })

  it('releases the renderer and WebGL when drawing the loaded frame fails', async () => {
    const failedRenderer = {
      renderPreviewForDisplay: vi.fn(() => { throw new Error('draw failed') }),
      dispose: vi.fn(),
    }
    loadSharedRenderer.mockResolvedValueOnce(failedRenderer)

    const pinia = createPinia()
    const store = useConfiguratorStore(pinia)
    vi.spyOn(store, 'initializeForecast').mockResolvedValue(false)
    vi.spyOn(store, 'initializeTide').mockResolvedValue(false)
    vi.spyOn(store, 'initializeNearbyDefault').mockResolvedValue(false)

    const wrapper = mount(LandingHero, { global: { plugins: [pinia] } })
    await vi.waitFor(() => expect(failedRenderer.dispose).toHaveBeenCalledOnce())
    expect(projectiveScreen.dispose).toHaveBeenCalledOnce()

    wrapper.unmount()
    expect(failedRenderer.dispose).toHaveBeenCalledOnce()
    expect(projectiveScreen.dispose).toHaveBeenCalledOnce()
  })
})
