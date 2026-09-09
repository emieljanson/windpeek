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
    window.history.replaceState({}, '', '/')
    projectiveScreen.setFrame.mockClear()
    projectiveScreen.draw.mockClear()
    projectiveScreen.dispose.mockClear()
  })

  it('renders the swell landing with swell first, compact wind and its own data request', async () => {
    window.history.replaceState({}, '', '/?site=swell')
    const pinia = createPinia()
    const store = useConfiguratorStore(pinia)
    vi.spyOn(store, 'initializeForecast').mockResolvedValue(false)
    vi.spyOn(store, 'initializeTide').mockResolvedValue(false)
    vi.spyOn(store, 'initializeNearbyDefault').mockResolvedValue(false)
    const swellRequest = vi.spyOn(store, 'refreshSwell').mockResolvedValue(false)
    const wrapper = mount(LandingHero, { global: { plugins: [pinia] } })
    await vi.waitFor(() => expect(swellRequest).toHaveBeenCalledOnce())
    const renderer = await loadSharedRenderer()
    await vi.waitFor(() => expect(renderer.renderPreviewForDisplay).toHaveBeenCalledWith(
      expect.objectContaining({ windSize: 1, swellSize: 2, moduleOrder: [1, 0, 2, 3, 4], showTemperature: false }),
      expect.anything(),
    ))
    expect(wrapper.get('.hero-link').attributes('href')).toContain('site=swell')
    wrapper.unmount()
    window.history.replaceState({}, '', '/')
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
    const photo = wrapper.get('picture source')
    expect(wrapper.get('picture img').attributes('src')).toContain('windpeek-hero-yellow-v21-1672w.jpg')
    expect(photo.attributes('srcset')).toContain('windpeek-hero-yellow-v21-960w.webp 960w')
    expect(photo.attributes('srcset')).toContain('windpeek-hero-yellow-v21-1672w.webp 1672w')
    // Account for the calibrated 1.2x photo zoom, not just the visible crop.
    expect(photo.attributes('sizes')).toBe('(max-width: 666px) calc(120vw - 28.8px), 770.4px')
    expect(projectiveScreen.draw).toHaveBeenCalledWith(
      expect.any(Array),
      expect.objectContaining({ brightness: 1.18 }),
    )
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
