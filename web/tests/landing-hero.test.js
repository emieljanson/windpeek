import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushPromises, mount } from '@vue/test-utils'
import LandingHero from '../src/components/LandingHero.vue'

vi.mock('../src/components/LandingForecast.vue', () => ({
  default: { template: '<canvas data-testid="live-forecast" />' },
}))

describe('lightweight landing hero', () => {
  let idle
  beforeEach(() => {
    window.history.replaceState({}, '', '/')
    vi.spyOn(HTMLImageElement.prototype, 'complete', 'get').mockReturnValue(false)
    vi.stubGlobal('requestIdleCallback', vi.fn(callback => { idle = callback; return 42 }))
    vi.stubGlobal('cancelIdleCallback', vi.fn())
  })
  afterEach(() => {
    vi.restoreAllMocks()
    vi.unstubAllGlobals()
    vi.useRealTimers()
  })

  it('shows the responsive photo and working link before loading live data', async () => {
    window.history.replaceState({}, '', '/?site=swell')
    const wrapper = mount(LandingHero)
    expect(wrapper.get('.hero-link').attributes('href')).toContain('site=swell')
    expect(wrapper.get('img').attributes('src')).toContain('windpeek-hero-yellow-v21-1672w.jpg')
    expect(wrapper.get('source').attributes('srcset')).toContain('windpeek-hero-yellow-v21-960w.webp 960w')
    expect(wrapper.get('source').attributes('sizes')).toBe('(max-width: 666px) calc(120vw - 28.8px), 770.4px')
    expect(wrapper.find('canvas').exists()).toBe(false)
    expect(window.requestIdleCallback).not.toHaveBeenCalled()
    await wrapper.get('img').trigger('load')
    expect(wrapper.find('canvas').exists()).toBe(false)
    await idle()
    await flushPromises()
    expect(wrapper.find('[data-testid="live-forecast"]').exists()).toBe(true)
    wrapper.unmount()
  })

  it('schedules a cached photo only once', async () => {
    vi.spyOn(HTMLImageElement.prototype, 'complete', 'get').mockReturnValue(true)
    const wrapper = mount(LandingHero)
    await wrapper.get('img').trigger('load')
    expect(window.requestIdleCallback).toHaveBeenCalledOnce()
    wrapper.unmount()
  })

  it('cancels a queued enhancement when navigating away', async () => {
    const wrapper = mount(LandingHero)
    await wrapper.get('img').trigger('load')
    wrapper.unmount()
    expect(window.cancelIdleCallback).toHaveBeenCalledWith(42)
  })

  it('still enhances after an image error in browsers without idle callbacks', async () => {
    vi.stubGlobal('requestIdleCallback', undefined)
    vi.useFakeTimers()
    const wrapper = mount(LandingHero)
    await wrapper.get('img').trigger('error')
    await vi.runAllTimersAsync()
    await flushPromises()
    expect(wrapper.find('canvas').exists()).toBe(true)
    wrapper.unmount()
  })
})
