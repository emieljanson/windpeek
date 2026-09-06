import { describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia } from 'pinia'
import { useConfiguratorStore } from '../src/stores/configurator'

const { toastError, toastDismiss } = vi.hoisted(() => ({
  toastError: vi.fn(),
  toastDismiss: vi.fn(),
}))

vi.mock('vue-sonner', () => ({
  Toaster: { template: '<div data-testid="toaster" />' },
  toast: { error: toastError, dismiss: toastDismiss },
}))

vi.mock('../src/views/ConfiguratorView.vue', () => ({
  default: { template: '<main />' },
}))

vi.mock('../src/composables/useCompactViewport', () => ({
  useCompactViewport: () => ({ isCompact: { value: false } }),
}))

import ConfiguratorApp from '../src/ConfiguratorApp.vue'

describe('configurator forecast toast', () => {
  it('dismisses an earlier warning as soon as the forecast leaves warning state', async () => {
    const pinia = createPinia()
    const store = useConfiguratorStore(pinia)
    const wrapper = mount(ConfiguratorApp, { global: { plugins: [pinia] } })

    store.$patch({ forecastStatus: 'warning', forecastMessage: 'Forecast failed' })
    await wrapper.vm.$nextTick()
    expect(toastError).toHaveBeenCalledWith('Forecast failed', { id: 'forecast-error' })

    store.$patch({ forecastStatus: 'ready' })
    await wrapper.vm.$nextTick()
    expect(toastDismiss).toHaveBeenCalledWith('forecast-error')

    wrapper.unmount()
  })
})
