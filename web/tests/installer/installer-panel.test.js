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
    connect: vi.fn(), confirmDevice: vi.fn(), reconnect: vi.fn(), retrySetup: vi.fn(),
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
  it.each([
    [BOARD_IDS.E1003, 'webusb', 'serial'],
    [BOARD_IDS.E1002, 'serial', 'webusb'],
  ])('chooses the tested Mac connection for %s with a recovery option', async (boardId, preferred, fallback) => {
    const originalUsb = Object.getOwnPropertyDescriptor(window.navigator, 'usb')
    const originalSerial = Object.getOwnPropertyDescriptor(window.navigator, 'serial')
    const originalUserAgent = Object.getOwnPropertyDescriptor(window.navigator, 'userAgent')
    Object.defineProperties(window.navigator, {
      usb: { configurable: true, value: { requestDevice: vi.fn() } },
      serial: { configurable: true, value: { requestPort: vi.fn() } },
      userAgent: { configurable: true, value: 'Macintosh' },
    })
    try {
      const session = fakeSession()
      session.connect.mockResolvedValue({ phase: 'ready' })
      mountPanel(session, { digest: 'wanted', boardId })
      expect(wrapper.text()).not.toContain('Try another connection')
      await wrapper.get('.installer-primary').trigger('click')
      expect(session.connect).toHaveBeenCalledWith(preferred)
      await vi.waitFor(() => expect(wrapper.get('h2').text()).toBe('Device not listed?'))
      expect(wrapper.get('.installer-step [role="status"]').text()).toContain('connected')
      expect(wrapper.get('.installer-primary').text()).toBe('Try another connection')
      expect(wrapper.text()).toContain(boardId === BOARD_IDS.E1003 ? 'E1003' : 'E1002')
      await wrapper.get('.installer-primary').trigger('click')
      expect(session.connect).toHaveBeenLastCalledWith(fallback)
      await vi.waitFor(() => expect(wrapper.get('h2').text()).toBe('Device not listed?'))
      await wrapper.get('.installer-secondary').trigger('click')
      expect(session.connect).toHaveBeenCalledTimes(3)
      expect(session.connect).toHaveBeenLastCalledWith(fallback)
      await vi.waitFor(() => expect(wrapper.get('h2').text()).toBe('Device not listed?'))
      await wrapper.get('.installer-primary').trigger('click')
      expect(session.connect).toHaveBeenCalledTimes(4)
      expect(session.connect).toHaveBeenLastCalledWith(preferred)
    } finally {
      for (const [key, descriptor] of Object.entries({ usb: originalUsb, serial: originalSerial, userAgent: originalUserAgent })) {
        if (descriptor) Object.defineProperty(window.navigator, key, descriptor)
        else delete window.navigator[key]
      }
    }
  })
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
      .toEqual(['E1003Our pick', 'E1002', 'E1001'])
    const screens = [...dialog.querySelectorAll('.hardware-spec--screen dd')]
    expect(screens.map(node => [...node.querySelectorAll('.hardware-spec__line')].map(line => line.textContent.trim())))
      .toEqual([
        ['10.3″, 16 greys'],
        ['7.3″, 6 colours'],
        ['7.5″, 4 greys'],
      ])
    expect([...dialog.querySelectorAll('.hardware-spec--resolution dd')].map(node => node.textContent.trim()))
      .toEqual(['High-res screen', 'Standard screen', 'Standard screen'])

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

    expect(wrapper.get('h2').text()).toBe('Reconnect your device')
    expect(wrapper.text()).not.toContain('could not reconnect automatically')
    expect(wrapper.text()).toContain('Keep USB connected')
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

  it.each([
    ['reconnecting', 'Finding Windpeek'],
    ['checking-device', 'Checking device'],
  ])('keeps the %s step visible while Wi-Fi networks load', async (previousPhase, heading) => {
    let finishScan
    const session = fakeSession()
    session.scanNetworks.mockImplementation(() => new Promise((resolve) => { finishScan = resolve }))
    mountPanel(session)

    session.emit({ phase: previousPhase, progress: 0.8, safeToDisconnect: true, error: null })
    await wrapper.vm.$nextTick()
    session.emit({ phase: 'wifi', progress: 0.8, safeToDisconnect: true, error: null })
    await wrapper.vm.$nextTick()

    expect(wrapper.get('.installer-layer').attributes('data-phase')).toBe(previousPhase)
    expect(wrapper.text()).toContain(heading)
    expect(wrapper.find('.installer-wifi').exists()).toBe(false)

    finishScan([{ ssid: 'Windpeek Studio', secured: true }])
    await vi.waitFor(() => expect(wrapper.find('.installer-wifi').exists()).toBe(true))
    expect(wrapper.get('.installer-layer').attributes('data-phase')).toBe('wifi')
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
  ])('keeps report delivery out of the %s recovery text', async (phase, message) => {
    const session = fakeSession({
      phase,
      progress: 0.8,
      safeToDisconnect: true,
      error: { message },
      diagnosticStatus: 'sending',
      diagnosticReference: null,
    })
    mountPanel(session)

    expect(sonner.loading).not.toHaveBeenCalled()
    expect(sonner.error).not.toHaveBeenCalled()
    expect(wrapper.find('details').exists()).toBe(false)
    expect(wrapper.text()).not.toContain('Sending report')
    expect(wrapper.get('a[href^="mailto:"]').element.closest('p')).toBeTruthy()
    expect(wrapper.find('.installer-diagnostic-status').exists()).toBe(false)
    expect(wrapper.findAll('[role="alert"]')).toHaveLength(phase === 'error' ? 1 : 0)
  })

  it('includes the confirmed reference in support email without showing it in the body', () => {
    const session = fakeSession({
      phase: 'error',
      progress: 0,
      safeToDisconnect: true,
      error: { message: 'USB access failed.' },
      diagnosticStatus: 'sent',
      diagnosticReference: 'WS-TEST123456',
    })
    mountPanel(session)

    expect(wrapper.text()).not.toContain('WS-TEST123456')
    expect(decodeURIComponent(wrapper.get('a[href^="mailto:"]').attributes('href'))).toContain('WS-TEST123456')
    expect(wrapper.find('.installer-connection-state').exists()).toBe(false)
    expect(wrapper.get('[role="alert"]').classes()).not.toContain('is-error')
    expect(wrapper.get('[data-testid="installer-state-icon"]').attributes('data-phase')).toBe('error')
    expect(sonner.success).not.toHaveBeenCalled()
    expect(wrapper.get('.installer-primary').text()).toBe('Try again')
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

    expect(sonner.error).not.toHaveBeenCalled()
    expect(wrapper.find('.installer-diagnostic-status').exists()).toBe(false)
    expect(wrapper.text()).not.toMatch(/WS-[0-9A-Z]{10}/)
  })

  it.each(['error', 'reconnect', 'wifi', 'verification-issue'])('offers the saved report when Sentry delivery fails during %s', (phase) => {
    const report = JSON.stringify({ version: 1, deviceEvidence: [{ kind: 'abort', addresses: [0x42001234] }] })
    mountPanel(fakeSession({ phase, safeToDisconnect: true, error: { message: 'USB connection lost' },
      diagnosticStatus: 'failed', diagnosticReport: report }))
    const link = wrapper.get('a[download="windpeek-diagnostic.json"]')
    expect(link.text()).toBe('Download the report')
    expect(decodeURIComponent(link.attributes('href').split(',')[1])).toBe(report)
    expect(wrapper.text()).not.toContain('Report not sent')
    const email = wrapper.get('a[href^="mailto:"]')
    expect(email.text()).toBe('email it to support')
    expect(decodeURIComponent(email.attributes('href'))).toContain('attach it to this email')
  })

  it('does not claim a report was downloaded when no report is available', () => {
    mountPanel(fakeSession({ phase: 'error', safeToDisconnect: true, error: { message: 'Stopped' } }))
    const email = decodeURIComponent(wrapper.get('a[href^="mailto:"]').attributes('href'))
    expect(email).toContain('Please help me finish setting up my Windpeek.')
    expect(email).not.toContain('downloaded')
  })

  it.each(['sending', 'sent', 'failed'])('keeps the report downloadable while delivery is %s', (status) => {
    mountPanel(fakeSession({ phase: 'error', safeToDisconnect: true, error: { message: 'Stopped' },
      diagnosticStatus: status, diagnosticReport: '{"version":1}' }))
    expect(wrapper.get('a[download]').attributes('href')).toContain('data:application/json')
    expect(wrapper.find('details').exists()).toBe(false)
    expect(wrapper.text()).not.toMatch(/Report (sent|pending|not sent)/)
  })

  it('retries setup directly when USB is still available', async () => {
    const session = fakeSession({ phase: 'verification-issue', safeToDisconnect: true, canRetrySetup: true,
      error: { message: 'Forecast unavailable' } })
    mountPanel(session)
    expect(wrapper.get('.installer-primary').text()).toBe('Try again')
    await wrapper.get('.installer-primary').trigger('click')
    expect(session.retrySetup).toHaveBeenCalledOnce()
    expect(session.reconnect).not.toHaveBeenCalled()
  })

  it('checks the device from a verification issue without showing the Wi-Fi form', async () => {
    const session = fakeSession({ phase: 'verification-issue', safeToDisconnect: true,
      error: { message: 'Wi-Fi connected, but setup could not be confirmed.' } })
    mountPanel(session)

    expect(wrapper.get('h2').text()).toBe('Setup didn’t finish')
    expect(wrapper.find('.installer-wifi').exists()).toBe(false)
    await wrapper.get('.installer-primary').trigger('click')
    expect(session.reconnect).toHaveBeenCalledOnce()
  })

  it('keeps a rejected Wi-Fi test visible without replacing it with a fresh scan', async () => {
    const session = fakeSession({ phase: 'configuring', safeToDisconnect: true, error: null })
    mountPanel(session)
    session.emit({ phase: 'wifi', safeToDisconnect: true,
      error: { message: 'Could not connect. Check the network and password.' } })
    await wrapper.vm.$nextTick()

    expect(session.scanNetworks).not.toHaveBeenCalled()
    expect(wrapper.get('#installer-wifi-error').text()).toContain('Could not connect')
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


it('lets an interrupted installation restart from the error screen', async () => {
  const session = fakeSession({ phase: 'error', safeToDisconnect: true, error: { message: 'Setup could not finish.' } })
  session.isDemo = true
  mountPanel(session)
  await wrapper.get('.installer-primary').trigger('click')
  expect(session.connect).toHaveBeenCalledOnce()
  expect(wrapper.emitted('close')).toBeUndefined()
})
