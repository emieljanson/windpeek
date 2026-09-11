import { afterEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import InstallerPanel from '../../src/components/installer/InstallerPanel.vue'
import ReTerminalHelpDialog from '../../src/components/ReTerminalHelpDialog.vue'
import SettingSelect from '../../src/components/settings/SettingSelect.vue'
import { BOARD_IDS } from '../../src/config/configuration'

const sonner = vi.hoisted(() => ({
  dismiss: vi.fn(),
  error: vi.fn(),
  loading: vi.fn(),
  success: vi.fn(),
}))
vi.mock('vue-sonner', () => ({ toast: sonner }))

let wrapper
afterEach(() => { wrapper?.unmount(); wrapper = undefined; vi.clearAllMocks() })

function fakeSession(initial = { phase: 'ready', progress: 0, safeToDisconnect: true, error: null }) {
  let listener
  const session = {
    subscribe: vi.fn((next) => { listener = next; next(initial); return () => {} }),
    connect: vi.fn(), confirmDevice: vi.fn(), reconnect: vi.fn(),
    scanNetworks: vi.fn().mockResolvedValue([]), submitWifi: vi.fn(), cancel: vi.fn(),
    emit(next) { listener(next) },
  }
  return session
}

function mountPanel(session, configuration = { digest: 'wanted' }) {
  wrapper = mount(InstallerPanel, {
    props: { configuration, sessionFactory: () => session },
    attachTo: document.body,
  })

  return wrapper
}

describe('installer inspector panel', () => {
  it('opens the reTerminal chooser from the E1003 connection step', async () => {
    const session = fakeSession()
    session.isDemo = true
    mountPanel(session, { digest: 'wanted', boardId: BOARD_IDS.E1003 })

    expect(wrapper.text()).toContain('Connect your reTerminal E1003')
    expect(wrapper.get('.installer-secondary').attributes('aria-haspopup')).toBe('dialog')
    await wrapper.get('.installer-secondary').trigger('click')

    expect(wrapper.findComponent(ReTerminalHelpDialog).props('open')).toBe(true)
    const dialog = document.body.querySelector('[role="dialog"]')
    expect([...dialog.querySelectorAll('.hardware-model__name')].map(node => node.textContent))
      .toEqual(['E1001', 'E1002', 'E1003'])
    const screens = [...dialog.querySelectorAll('.hardware-spec--screen dd')]
    expect(screens.map(node => [...node.querySelectorAll('.hardware-spec__line')].map(line => line.textContent.trim())))
      .toEqual([
        ['7.5″, 4 greys'],
        ['7.3″, 6 colours'],
        ['10.3″, 16 greys'],
      ])
    expect([...dialog.querySelectorAll('.hardware-spec--resolution dd')].map(node => node.textContent.trim()))
      .toEqual(['Standard screen', 'Standard screen', 'High-res screen'])

    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('close')).toBeUndefined()
  })

  it('keeps one icon grid mounted while step content changes around it', async () => {
    const session = fakeSession()
    mountPanel(session)

    const icon = wrapper.get('[data-testid="installer-state-icon"]')
    const element = icon.element
    expect(icon.attributes('data-phase')).toBe('error')
    expect(icon.findAll('.installer-state-icon__cell')).toHaveLength(81)
    expect(icon.element.closest('.installer-stage')).toBeNull()

    session.emit({ phase: 'downloading', progress: 0, safeToDisconnect: true, error: null, action: { action: 'install' } })
    await vi.waitFor(() => expect(wrapper.get('[data-testid="installer-state-icon"]').attributes('data-phase')).toBe('downloading'))

    expect(wrapper.get('[data-testid="installer-state-icon"]').element).toBe(element)
  })

  it('shows an honest supported-browser route before requesting permission', () => {
    const session = fakeSession()
    mountPanel(session)
    expect(wrapper.get('h2').text()).toBe('Use Firefox, Chrome, or Edge')
    expect(wrapper.get('.installer-step').classes()).toContain('installer-step--connect')
    expect(wrapper.text()).toContain('Update to a current desktop version of Firefox, Chrome, or Edge')
    expect(wrapper.get('[data-testid="installer-state-icon"]').attributes('data-phase')).toBe('error')
    expect(wrapper.get('.installer-layer').attributes('data-phase')).toBe('error')
    expect(wrapper.text()).not.toContain('USB Serial')
    expect(wrapper.text()).not.toMatch(/Espressif|USB JTAG/i)
    expect(wrapper.find('.installer-primary').exists()).toBe(false)
    expect(session.connect).not.toHaveBeenCalled()
  })

  it('continues to device selection when Web Serial is available', async () => {
    const originalSerialDescriptor = Object.getOwnPropertyDescriptor(window.navigator, 'serial')
    Object.defineProperty(window.navigator, 'serial', {
      configurable: true,
      value: { requestPort: vi.fn() },
    })

    try {
      const session = fakeSession()
      mountPanel(session)
      expect(wrapper.text()).not.toContain('Firefox, Chrome, or Edge')
      const buyButton = wrapper.get('.installer-secondary')
      expect(buyButton.element.tagName).toBe('BUTTON')
      expect(buyButton.text()).toBe('Buy a reTerminal')
      expect(buyButton.element.nextElementSibling).toBe(wrapper.get('.installer-primary').element)
      await buyButton.trigger('click')
      expect(wrapper.findComponent(ReTerminalHelpDialog).props('open')).toBe(true)
      wrapper.findComponent(ReTerminalHelpDialog).vm.$emit('update:open', false)
      await wrapper.vm.$nextTick()
      expect(wrapper.get('.installer-primary').text()).toBe('Continue')
      await wrapper.get('.installer-primary').trigger('click')
      expect(session.connect).toHaveBeenCalledOnce()
    } finally {
      if (originalSerialDescriptor) Object.defineProperty(window.navigator, 'serial', originalSerialDescriptor)
      else delete window.navigator.serial
    }
  })

  it('keeps the USB scene active only during the physical connection step', async () => {
    const session = fakeSession()
    mountPanel(session)

    expect(wrapper.emitted('usb-step-change')).toEqual([[true]])

    session.emit({ phase: 'choosing-device', progress: 0, safeToDisconnect: true, error: null })
    await wrapper.vm.$nextTick()

    expect(wrapper.emitted('usb-step-change')).toEqual([[true], [false]])
  })

  it('uses the animated state icon without implying measurable progress while checking the device', () => {
    const session = fakeSession({ phase: 'checking-device', progress: 0.02, safeToDisconnect: true, error: null })
    mountPanel(session)

    expect(wrapper.get('h2').text()).toBe('Checking device')
    expect(wrapper.find('[role="progressbar"]').exists()).toBe(false)
    expect(wrapper.get('[data-testid="installer-state-icon"]').attributes('data-phase')).toBe('checking-device')
  })

  it('offers manual USB selection only after automatic reconnect falls back', () => {
    const session = fakeSession({ phase: 'reconnect', progress: 0.78, safeToDisconnect: true, error: null })
    mountPanel(session)

    expect(wrapper.get('h2').text()).toBe('Select your reTerminal again')
    expect(wrapper.text()).toContain('could not reconnect')
    expect(wrapper.text()).not.toContain('could not reconnect automatically')
    expect(wrapper.text()).toContain('Keep the cable connected')
    expect(wrapper.get('.installer-primary').text()).toBe('Choose USB device')
    expect(wrapper.text()).not.toContain('Reconnect device')
  })

  it('keeps the browser device chooser instruction concise', () => {
    const session = fakeSession({ phase: 'choosing-device', progress: 0, safeToDisconnect: true, error: null })
    mountPanel(session)

    expect(wrapper.get('h2').text()).toBe('Select your reTerminal')
    expect(wrapper.get('.installer-step__copy p').text()).toBe('In the browser window, select the connected device. It may appear as USB Serial or a similar USB name.')
    expect(wrapper.findAll('.installer-step__copy p')).toHaveLength(1)
  })

  it('combines enclosure and install confirmation for an unverified ESP32-S3', async () => {
    const session = fakeSession({ phase: 'confirm-device', progress: 0, safeToDisconnect: true, error: null })
    mountPanel(session)
    expect(wrapper.text()).toContain('Confirm your reTerminal')
    expect(wrapper.text()).toContain('reTerminal E1002')
    expect(wrapper.text()).toContain('replace its software and saved setup')
    expect(wrapper.get('.installer-primary').text()).toBe('Install Windpeek')
    expect(wrapper.find('.installer-secondary').exists()).toBe(false)
    expect(wrapper.find('.installer-device').exists()).toBe(false)
    await wrapper.get('.installer-primary').trigger('click')
    expect(session.confirmDevice).toHaveBeenCalledOnce()
  })

  it('does not show a redundant review action while known-device work starts', () => {
    const session = fakeSession({
      phase: 'downloading',
      progress: 0.05,
      safeToDisconnect: true,
      error: null,
      action: { action: 'update-firmware' },
    })
    mountPanel(session)

    expect(wrapper.get('.installer-back').exists()).toBe(true)
    expect(wrapper.get('h2').text()).toBe('Preparing firmware')
    expect(wrapper.find('.installer-primary').exists()).toBe(false)
  })

  it('reuses the inspector select for scanned Wi-Fi networks', async () => {
    const session = fakeSession()
    session.scanNetworks.mockResolvedValue([
      { ssid: 'A very long network name that needs truncating', rssi: -40, secured: true },
    ])
    mountPanel(session)
    session.emit({ phase: 'wifi', progress: 0.8, safeToDisconnect: true, error: null })
    await vi.waitFor(() => expect(wrapper.findComponent(SettingSelect).exists()).toBe(true))

    const select = wrapper.getComponent(SettingSelect)
    expect(select.get('.setting-select__value').exists()).toBe(true)
    expect(select.get('.setting-select__chevron').exists()).toBe(true)
    expect(wrapper.findAll('.installer-fields [role="combobox"]')).toHaveLength(1)
    select.vm.$emit('update:modelValue', 'A very long network name that needs truncating')
    await wrapper.get('input[name="wifi-password"]').setValue('super-secret')
    await wrapper.get('form').trigger('submit')

    expect(session.submitWifi).toHaveBeenCalledWith({
      ssid: 'A very long network name that needs truncating',
      password: 'super-secret',
    })
  })

  it('allows an open network without weakening password checks for secured networks', async () => {
    const session = fakeSession()
    session.scanNetworks.mockResolvedValue([
      { ssid: 'Open guest network', rssi: -42, secured: false },
      { ssid: 'Secured home network', rssi: -48, secured: true },
    ])
    mountPanel(session)
    session.emit({ phase: 'wifi', progress: 0.8, safeToDisconnect: true, error: null })
    await vi.waitFor(() => expect(wrapper.findComponent(SettingSelect).exists()).toBe(true))

    const select = wrapper.getComponent(SettingSelect)
    select.vm.$emit('update:modelValue', 'Secured home network')
    await wrapper.vm.$nextTick()
    expect(wrapper.get('input[name="wifi-password"]').attributes('required')).toBeDefined()
    expect(wrapper.get('.installer-primary').attributes('disabled')).toBeDefined()
    await wrapper.get('input[name="wifi-password"]').setValue('must-not-leak')

    select.vm.$emit('update:modelValue', 'Open guest network')
    await wrapper.vm.$nextTick()
    expect(wrapper.get('input[name="wifi-password"]').attributes('required')).toBeUndefined()
    expect(wrapper.get('.installer-primary').attributes('disabled')).toBeUndefined()
    await wrapper.get('form').trigger('submit')
    expect(session.submitWifi).toHaveBeenLastCalledWith({
      ssid: 'Open guest network',
      password: '',
    })

    select.vm.$emit('update:modelValue', 'Secured home network')
    await wrapper.vm.$nextTick()
    expect(wrapper.get('input[name="wifi-password"]').attributes('required')).toBeDefined()
    expect(wrapper.get('.installer-primary').attributes('disabled')).toBeDefined()
  })

  it('keeps password managers away from the WiFi credential field', () => {
    const session = fakeSession({ phase: 'wifi', progress: 0.8, safeToDisconnect: true, error: null })
    mountPanel(session)

    const passwordInput = wrapper.get('input[name="wifi-password"]')
    expect(passwordInput.attributes('autocomplete')).toBe('off')
    expect(passwordInput.attributes('data-1p-ignore')).toBe('true')
    expect(passwordInput.attributes('data-lpignore')).toBe('true')
    expect(passwordInput.attributes('data-bwignore')).toBe('true')
    expect(passwordInput.attributes('data-form-type')).toBe('other')
  })

  it('locks close and names the unsafe state while firmware is writing', () => {
    const session = fakeSession({ phase: 'installing-firmware', progress: 0.5, safeToDisconnect: false, error: null })
    mountPanel(session)
    expect(wrapper.get('.installer-back').attributes('disabled')).toBeDefined()
    expect(wrapper.get('.installer-step__copy').text()).toContain('Keep the USB cable connected until writing is complete.')
    expect(wrapper.find('.installer-connection-state').exists()).toBe(false)
    expect(wrapper.get('[role="progressbar"]').attributes('aria-valuenow')).toBe('50')
  })

  it('clears both credential inputs immediately after Wi-Fi submission', async () => {
    const session = fakeSession({ phase: 'wifi', progress: 0.8, safeToDisconnect: true, error: null })
    mountPanel(session)
    await wrapper.get('input[name="ssid"]').setValue('Home')
    await wrapper.get('input[name="wifi-password"]').setValue('super-secret')
    await wrapper.get('form').trigger('submit')
    expect(session.submitWifi).toHaveBeenCalledWith({ ssid: 'Home', password: 'super-secret' })
    expect(wrapper.get('input[name="ssid"]').element.value).toBe('')
    expect(wrapper.get('input[name="wifi-password"]').element.value).toBe('')
    expect(JSON.stringify(wrapper.html())).not.toContain('super-secret')
  })

  it.each([
    ['error', 'USB access failed.'],
    ['reconnect', 'Windpeek disconnected.'],
    ['wifi', 'Windpeek could not connect.'],
  ])('shows diagnostic delivery beside a %s recovery state', async (phase, message) => {
    const session = fakeSession({
      phase,
      progress: 0.8,
      safeToDisconnect: true,
      error: { message },
      diagnosticStatus: 'sending',
      diagnosticReference: null,
    })
    mountPanel(session)

    expect(sonner.loading).toHaveBeenCalledWith('Sending technical details…', { id: 'installer-diagnostics' })
    expect(sonner.error).toHaveBeenCalledWith(message, { id: 'installer-error', duration: 5000 })
    expect(wrapper.find('.installer-diagnostic-status').exists()).toBe(false)
    expect(wrapper.findAll('[role="alert"]')).toHaveLength(phase === 'error' ? 1 : 0)
  })

  it('shows only a confirmed, selectable diagnostic reference', () => {
    const session = fakeSession({
      phase: 'error',
      progress: 0,
      safeToDisconnect: true,
      error: { message: 'USB access failed.' },
      diagnosticStatus: 'sent',
      diagnosticReference: 'WS-TEST123456',
    })
    mountPanel(session)

    const status = wrapper.get('.installer-diagnostic-status')
    expect(status.text()).toBe('Diagnostic reference: WS-TEST123456')
    expect(status.get('code').text()).toBe('WS-TEST123456')
    expect(status.element.parentElement).toBe(wrapper.get('.installer-step__copy').element)
    expect(wrapper.get('.installer-connection-state').element.parentElement).toBe(wrapper.get('.installer-step__copy').element)
    expect(wrapper.get('[role="alert"]').classes()).not.toContain('is-error')
    expect(wrapper.get('[data-testid="installer-state-icon"]').attributes('data-phase')).toBe('error')
    expect(sonner.success).toHaveBeenCalledWith('Technical details sent', {
      id: 'installer-diagnostics',
      description: 'Diagnostic reference: WS-TEST123456',
      duration: 8000,
    })
    expect(wrapper.get('.installer-primary').text()).toBe('Close')
  })

  it('does not invent a reference when diagnostic delivery fails', () => {
    const session = fakeSession({
      phase: 'error',
      progress: 0,
      safeToDisconnect: true,
      error: { message: 'USB access failed.' },
      diagnosticStatus: 'failed',
      diagnosticReference: null,
    })
    mountPanel(session)

    expect(sonner.error).toHaveBeenCalledWith('Technical details could not be sent.', {
      id: 'installer-diagnostics',
      duration: 5000,
    })
    expect(wrapper.find('.installer-diagnostic-status').exists()).toBe(false)
    expect(wrapper.text()).not.toMatch(/WS-[0-9A-Z]{10}/)
  })

  it('closes with Escape only when disconnecting is safe', async () => {
    const safe = fakeSession()
    mountPanel(safe)
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))
    await Promise.resolve()
    expect(wrapper.emitted('close')).toHaveLength(1)
    wrapper.unmount()

    const unsafe = fakeSession({ phase: 'installing-firmware', progress: 0.3, safeToDisconnect: false, error: null })
    mountPanel(unsafe)
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))
    expect(wrapper.emitted('close')).toBeUndefined()
  })
})
