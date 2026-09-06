import { describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia } from 'pinia'
import InstallerPanel from '../src/components/installer/InstallerPanel.vue'
import { useConfiguratorStore } from '../src/stores/configurator'

const { fetchForecast, fetchTide } = vi.hoisted(() => ({
  fetchForecast: vi.fn().mockRejectedValue(new Error('offline')),
  fetchTide: vi.fn().mockRejectedValue(new Error('offline')),
}))

vi.mock('../src/forecast/openMeteo', () => ({
  fetchOpenMeteoForecasts: fetchForecast,
}))

vi.mock('../src/forecast/openMeteoMarine', () => ({
  fetchOpenMeteoTide: fetchTide,
}))

import ConfiguratorView from '../src/views/ConfiguratorView.vue'

describe('configurator experience', () => {
  it.each([
    ['', true],
    ['?devicePreview=seeedstudio_reterminal_e1002', false],
    ['?installerDemo=1', false],
  ])('nearby lookup eligibility for route %s', async (search, expected) => {
    const originalUrl = window.location.href
    window.history.replaceState({}, '', `/${search}`)
    const pinia = createPinia()
    const store = useConfiguratorStore(pinia)
    const initializeForecast = vi.spyOn(store, 'initializeForecast').mockResolvedValue(false)
    const initializeTide = vi.spyOn(store, 'initializeTide').mockResolvedValue(false)
    const initializeNearbyDefault = vi.spyOn(store, 'initializeNearbyDefault').mockResolvedValue(false)

    try {
      const wrapper = mount(ConfiguratorView, {
        global: {
          plugins: [pinia],
          stubs: {
            WindScoutScene: { template: '<div data-testid="3d-scene"></div>' },
            WindScoutSettings: { template: '<div></div>' },
            InstallContinuation: { template: '<div></div>' },
          },
        },
      })
      await wrapper.vm.$nextTick()
      expect(initializeNearbyDefault).toHaveBeenCalledTimes(expected ? 1 : 0)
      if (expected) {
        expect(initializeForecast.mock.invocationCallOrder[0]).toBeLessThan(
          initializeNearbyDefault.mock.invocationCallOrder[0],
        )
        expect(initializeTide.mock.invocationCallOrder[0]).toBeLessThan(
          initializeNearbyDefault.mock.invocationCallOrder[0],
        )
      }
      wrapper.unmount()
    } finally {
      window.history.replaceState({}, '', originalUrl)
    }
  })

  it('keeps the 3D product and controls as the only configurator view', () => {
    const wrapper = mount(ConfiguratorView, {
      global: {
        plugins: [createPinia()],
        stubs: { WindScoutScene: { template: '<div data-testid="3d-scene"></div>' } },
      },
    })
    expect(wrapper.find('[data-testid="3d-scene"]').exists()).toBe(true)
    expect(wrapper.find('[data-testid="flat-preview"]').exists()).toBe(false)
    expect(wrapper.find('.settings-panel').exists()).toBe(true)
    expect(wrapper.findAll('.setting-section__title')).toHaveLength(0)
    expect(wrapper.get('.inspector-search input').attributes('placeholder')).toBe('Search spot…')
    expect(wrapper.text()).not.toContain('See your next session')
    expect(wrapper.text()).not.toContain('Reset view')
  })

  it('starts the default Brouwersdam forecast without a device connection', async () => {
    fetchForecast.mockRejectedValueOnce(new Error('offline'))
    const wrapper = mount(ConfiguratorView, {
      global: {
        plugins: [createPinia()],
        stubs: { WindScoutScene: { template: '<div data-testid="3d-scene"></div>' } },
      },
    })
    await vi.waitFor(() => expect(fetchForecast).toHaveBeenCalled())
    expect(fetchForecast.mock.calls.at(-1)[0]).toMatchObject({ id: 'brouwersdam' })
    expect(wrapper.get('[data-testid="forecast-label"]').text()).toBe('Demo')
    await vi.waitFor(() => expect(wrapper.get('[role="status"]').text()).toContain('demo data'))
  })

  it('shows an honest error instead of replacing a failed 3D scene', async () => {
    const wrapper = mount(ConfiguratorView, {
      global: {
        plugins: [createPinia()],
        stubs: {
          WindScoutScene: {
            emits: ['error'],
            mounted() { this.$emit('error', 'The model is unavailable.') },
            template: '<div></div>',
          },
        },
      },
    })
    await wrapper.vm.$nextTick()
    expect(wrapper.get('[data-testid="scene-error"]').text()).toContain('The model is unavailable.')
    expect(wrapper.find('[data-testid="flat-preview"]').exists()).toBe(false)
  })

  it('opens the guided installer inside the inspector before requesting a device', async () => {
    const originalSerialDescriptor = Object.getOwnPropertyDescriptor(window.navigator, 'serial')
    Object.defineProperty(window.navigator, 'serial', {
      configurable: true,
      value: { requestPort: vi.fn() },
    })

    try {
      const wrapper = mount(ConfiguratorView, {
        global: {
          plugins: [createPinia()],
          stubs: {
            WindScoutScene: {
              props: ['focusUsbConnection', 'showUsbCable'],
              template: '<div data-testid="3d-scene" :data-cable-visible="String(showUsbCable)" :data-usb-focused="String(focusUsbConnection)"></div>',
            },
          },
        },
      })
      expect(wrapper.find('.installer-layer').exists()).toBe(false)
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('data-cable-visible')).toBe('false')
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('data-usb-focused')).toBe('false')
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('firmware-transfer-active')).toBeUndefined()
      await wrapper.get('[data-testid="install-continuation"]').trigger('click')
      expect(wrapper.get('.installer-layer').text()).toContain('Connect your reTerminal')
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('data-cable-visible')).toBe('true')
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('data-usb-focused')).toBe('true')
      wrapper.getComponent(InstallerPanel).vm.$emit('usb-step-change', false)
      wrapper.getComponent(InstallerPanel).vm.$emit('installer-phase-change', 'installing-firmware')
      await wrapper.vm.$nextTick()
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('data-cable-visible')).toBe('true')
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('data-usb-focused')).toBe('false')
      expect(wrapper.get('[data-testid="3d-scene"]').attributes('firmware-transfer-active')).toBeUndefined()
      await wrapper.get('[aria-label="Back to configurator"]').trigger('click')
      await vi.waitFor(() => {
        expect(wrapper.get('[data-testid="3d-scene"]').attributes('data-cable-visible')).toBe('false')
      })
      wrapper.unmount()
    } finally {
      if (originalSerialDescriptor) Object.defineProperty(window.navigator, 'serial', originalSerialDescriptor)
      else delete window.navigator.serial
    }
  })

  it.each([
    { width: 390, mobile: true, compact: true, installer: false },
    { width: 320, mobile: false, compact: false, installer: true },
    { width: 640, mobile: false, compact: false, installer: true },
    { width: 896, mobile: false, compact: false, installer: true },
    { width: 897, mobile: false, compact: false, installer: true },
  ])('renders the right settings surface for $width px mobile=$mobile', async ({ width, mobile, compact, installer }) => {
    const originalWidth = window.innerWidth
    const originalMatchMedia = window.matchMedia
    const originalUserAgent = window.navigator.userAgent
    Object.defineProperty(window, 'innerWidth', { configurable: true, value: width })
    Object.defineProperty(window.navigator, 'userAgent', {
      configurable: true,
      value: mobile ? 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)' : 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)',
    })
    Object.defineProperty(window, 'matchMedia', {
      configurable: true,
      value: vi.fn(() => ({
        matches: compact,
        addEventListener: vi.fn(),
        removeEventListener: vi.fn(),
      })),
    })

    try {
      const wrapper = mount(ConfiguratorView, {
        global: {
          plugins: [createPinia()],
          stubs: {
            WindScoutScene: { template: '<div data-testid="3d-scene"></div>' },
            WindScoutSettings: { template: '<div data-testid="settings-surface"></div>' },
            InstallContinuation: { template: '<button data-testid="install-continuation">Install</button>' },
          },
        },
      })

      expect(wrapper.findAll('[data-testid="settings-surface"]')).toHaveLength(1)
      expect(wrapper.find('.mobile-settings-sheet').exists()).toBe(false)
      expect(wrapper.get('.settings-panel').classes()).toContain(compact ? 'settings-panel--compact' : 'settings-panel')
      expect(wrapper.find('[data-testid="install-continuation"]').exists()).toBe(installer)
      wrapper.unmount()
    } finally {
      Object.defineProperty(window, 'innerWidth', { configurable: true, value: originalWidth })
      Object.defineProperty(window, 'matchMedia', { configurable: true, value: originalMatchMedia })
      Object.defineProperty(window.navigator, 'userAgent', { configurable: true, value: originalUserAgent })
    }
  })

  it('keeps the compact panel above Safari chrome through the visual viewport inset', () => {
    const originalInnerHeight = window.innerHeight
    const originalVisualViewport = window.visualViewport
    const originalRequestAnimationFrame = window.requestAnimationFrame
    const originalCancelAnimationFrame = window.cancelAnimationFrame
    const listeners = new Map()
    const visualViewport = {
      height: 700,
      offsetTop: 20,
      addEventListener: vi.fn((type, handler) => listeners.set(type, handler)),
      removeEventListener: vi.fn(),
    }
    let scheduledCallback

    Object.defineProperty(window, 'innerHeight', { configurable: true, value: 844 })
    Object.defineProperty(window, 'visualViewport', { configurable: true, value: visualViewport })
    window.requestAnimationFrame = vi.fn((callback) => {
      scheduledCallback = callback
      return 1
    })
    window.cancelAnimationFrame = vi.fn()

    try {
      const wrapper = mount(ConfiguratorView, {
        global: {
          plugins: [createPinia()],
          stubs: { WindScoutScene: { template: '<div data-testid="3d-scene"></div>' } },
        },
      })
      scheduledCallback()

      expect(document.documentElement.style.getPropertyValue('--visual-viewport-bottom')).toBe('124px')
      expect(visualViewport.addEventListener).toHaveBeenCalledWith('resize', expect.any(Function))
      expect(visualViewport.addEventListener).toHaveBeenCalledWith('scroll', expect.any(Function))

      wrapper.unmount()
      expect(visualViewport.removeEventListener).toHaveBeenCalledWith('resize', listeners.get('resize'))
      expect(visualViewport.removeEventListener).toHaveBeenCalledWith('scroll', listeners.get('scroll'))
      expect(document.documentElement.style.getPropertyValue('--visual-viewport-bottom')).toBe('')
    } finally {
      Object.defineProperty(window, 'innerHeight', { configurable: true, value: originalInnerHeight })
      Object.defineProperty(window, 'visualViewport', { configurable: true, value: originalVisualViewport })
      window.requestAnimationFrame = originalRequestAnimationFrame
      window.cancelAnimationFrame = originalCancelAnimationFrame
      document.documentElement.style.removeProperty('--visual-viewport-bottom')
    }
  })
})
