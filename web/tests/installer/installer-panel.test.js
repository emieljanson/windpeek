import { flushPromises, mount } from '@vue/test-utils'
import { nextTick } from 'vue'
import { describe, expect, it } from 'vitest'
import InstallerPanel from '../../src/components/installer/InstallerPanel.vue'

describe('installer Wi-Fi handoff', () => {
  it('keeps the scanning step visible until network choices are ready', async () => {
    let publishState
    let finishScan
    const scan = new Promise((resolve) => { finishScan = resolve })
    const session = {
      subscribe(listener) {
        publishState = listener
        listener({ phase: 'ready', progress: 0, safeToDisconnect: true, error: null })
        return () => {}
      },
      scanNetworks: () => scan,
      cancel: async () => {},
    }
    const wrapper = mount(InstallerPanel, {
      props: { configuration: { boardId: 'seeedstudio_reterminal_e1003' }, sessionFactory: () => session },
      global: { stubs: { ReTerminalHelpDialog: true } },
    })

    publishState({ phase: 'wifi', progress: 0.8, safeToDisconnect: true, error: null })
    await nextTick()
    expect(wrapper.attributes('data-phase')).toBe('wifi-scanning')
    expect(wrapper.text()).toContain('Finding Wi-Fi networks')
    expect(wrapper.find('.installer-wifi').exists()).toBe(false)

    finishScan([{ ssid: 'Windpeek Studio', secured: true }])
    await flushPromises()
    expect(wrapper.attributes('data-phase')).toBe('wifi')
    expect(wrapper.find('.installer-wifi').exists()).toBe(true)
    expect(wrapper.find('[aria-label="Wi-Fi network"]').exists()).toBe(true)
    wrapper.unmount()
  })
})
