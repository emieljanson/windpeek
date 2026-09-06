import { expect, test } from '@playwright/test'

async function installFakeDevice(page) {
  await page.addInitScript(() => {
    globalThis.__WINDSCOUT_INSTALLER_SESSION_FACTORY__ = () => {
      let state = { phase: 'ready', progress: 0, safeToDisconnect: true, error: null }
      const listeners = new Set()
      const update = (patch) => { state = { ...state, ...patch }; listeners.forEach((listener) => listener(state)) }
      return {
        subscribe(listener) { listeners.add(listener); listener(state); return () => listeners.delete(listener) },
        async connect() { update({ phase: 'confirm-device' }) },
        confirmDevice() {
          update({ phase: 'installing-firmware', progress: 0.5, safeToDisconnect: false })
          // Keep the transient writing phase observable even when the full E2E
          // suite runs under load. The real installer remains in this phase for
          // the duration of the flash operation.
          setTimeout(() => {
            update({ phase: 'reconnecting', progress: 0.78, safeToDisconnect: true })
            setTimeout(() => update({ phase: 'wifi', progress: 0.8, safeToDisconnect: true }), 800)
          }, 5000)
        },
        async reconnect() { update({ phase: 'wifi', progress: 0.8, safeToDisconnect: true }) },
        async scanNetworks() { return [{ ssid: 'Windscout Test Network With A Very Long Name', rssi: -40, secured: true }] },
        async submitWifi() { update({ phase: 'complete', progress: 1, safeToDisconnect: true }) },
        async cancel() { state = { phase: 'ready', progress: 0, safeToDisconnect: true, error: null } },
      }
    }
  })
}

async function installFailingDevice(page) {
  await page.addInitScript(() => {
    globalThis.__WINDSCOUT_INSTALLER_SESSION_FACTORY__ = () => {
      let state = { phase: 'ready', progress: 0, safeToDisconnect: true, error: null, diagnosticStatus: 'idle', diagnosticReference: null }
      const listeners = new Set()
      const update = (patch) => { state = { ...state, ...patch }; listeners.forEach((listener) => listener(state)) }
      return {
        subscribe(listener) { listeners.add(listener); listener(state); return () => listeners.delete(listener) },
        async connect() {
          update({
            phase: 'error',
            error: { message: 'Windscout could not access the selected USB device.' },
            diagnosticStatus: 'sent',
            diagnosticReference: 'WS-TEST123456',
          })
        },
        async cancel() {},
      }
    }
  })
}

async function installWifiFailure(page) {
  await page.addInitScript(() => {
    globalThis.__WINDSCOUT_INSTALLER_SESSION_FACTORY__ = () => {
      let state = { phase: 'ready', progress: 0, safeToDisconnect: true, error: null, diagnosticStatus: 'idle', diagnosticReference: null }
      const listeners = new Set()
      const update = (patch) => { state = { ...state, ...patch }; listeners.forEach((listener) => listener(state)) }
      return {
        subscribe(listener) { listeners.add(listener); listener(state); return () => listeners.delete(listener) },
        async connect() {
          update({
            phase: 'wifi',
            progress: 0.8,
            error: { message: 'Windscout could not scan for Wi-Fi networks.' },
            diagnosticStatus: 'sent',
            diagnosticReference: 'WS-TEST123456',
          })
        },
        async scanNetworks() {
          return [
            { ssid: 'Windscout Studio', rssi: -35, secured: true },
            { ssid: 'North Sea Guest', rssi: -58, secured: false },
          ]
        },
        async submitWifi() {},
        async cancel() {},
      }
    }
  })
}

test('guides a fake E1002 through confirmation, reconnect, Wi-Fi and completion', async ({ page }) => {
  await installFakeDevice(page)
  await page.setViewportSize({ width: 1280, height: 720 })
  await page.goto('/?configure')

  const install = page.getByRole('button', { name: 'Install', exact: true })
  await install.click()
  expect(await page.locator('.installer-layer').evaluate((element) => getComputedStyle(element).transform)).toBe('none')
  await expect(page.getByRole('heading', { name: 'Connect your reTerminal' })).toBeVisible()
  await expect(page.locator('.installer-step--connect .installer-step__copy p')).toHaveCSS('font-size', '13px')
  await expect(page.locator('.installer-step--connect .installer-step__copy')).toHaveCSS('animation-name', 'installer-copy-enter')
  await expect(page.getByRole('button', { name: 'Continue' })).toBeFocused()
  const stateIcon = page.getByTestId('installer-state-icon')
  await page.waitForTimeout(250)
  const iconOrigin = await stateIcon.boundingBox()
  const copyOrigin = await page.locator('.installer-stage__view:not([aria-hidden="true"]) .installer-step__copy').boundingBox()
  const expectCopyAligned = async () => {
    await expect.poll(async () => {
      const bounds = await page.locator('.installer-stage__view:not([aria-hidden="true"]) .installer-step__copy').boundingBox()
      return Math.abs(bounds.y - copyOrigin.y)
    }).toBeLessThanOrEqual(2)
  }
  await expect(stateIcon).toHaveAttribute('data-phase', 'ready')

  await page.getByRole('button', { name: 'Continue' }).click()
  await expect(page.getByRole('heading', { name: 'Confirm your reTerminal' })).toBeVisible()
  await expect(page.getByText('Make sure this is a reTerminal E1002. Installing will replace its software and saved setup.')).toBeVisible()
  await expectCopyAligned()
  await expect(stateIcon).toHaveAttribute('data-phase', 'confirm-device')
  expect(await stateIcon.boundingBox()).toMatchObject({ x: iconOrigin.x, y: iconOrigin.y })
  await page.getByRole('button', { name: 'Install Windscout' }).click()
  await expect(page.getByRole('heading', { name: 'Writing firmware' })).toBeVisible()
  await expect(stateIcon).toHaveAttribute('data-phase', 'installing-firmware')
  await expect(page.getByText('Keep the USB cable connected until writing is complete.')).toBeVisible()
  expect(await stateIcon.boundingBox()).toMatchObject({ x: iconOrigin.x, y: iconOrigin.y })

  // Seven parallel 3D scenes can heavily delay browser timers in CI even
  // though the demo's configured firmware duration remains three seconds.
  await expect(page.getByRole('heading', { name: 'Finding Windscout' })).toBeVisible({ timeout: 30_000 })
  await expectCopyAligned()
  await expect(page.getByRole('heading', { name: 'Select a network for Windscout' })).toBeVisible()
  await expectCopyAligned()
  await expect(page.locator('.installer-field').first()).toHaveCSS('font-size', '13px')
  const network = page.getByRole('combobox', { name: 'Wi-Fi network' })
  await network.click()
  const networkOption = page.getByRole('option', { name: 'Windscout Test Network With A Very Long Name' })
  await expect(networkOption.locator('.setting-option__text')).toHaveCSS('text-overflow', 'ellipsis')
  await networkOption.click()
  const networkValue = network.locator('.setting-select__value')
  await expect(networkValue).toHaveCSS('text-overflow', 'ellipsis')
  const panelBounds = await page.getByRole('complementary', { name: 'Windscout settings' }).boundingBox()
  const networkBounds = await network.boundingBox()
  expect(networkBounds.x + networkBounds.width).toBeLessThanOrEqual(panelBounds.x + panelBounds.width - 12)
  const password = page.getByLabel('Password')
  const sharedControlStyle = async (control) => control.evaluate((element) => {
    const style = getComputedStyle(element)
    return {
      backgroundColor: style.backgroundColor,
      borderRadius: style.borderRadius,
      fontFamily: style.fontFamily,
      fontSize: style.fontSize,
      fontWeight: style.fontWeight,
      height: style.height,
    }
  })
  expect(await sharedControlStyle(password)).toEqual(await sharedControlStyle(network))
  await password.focus()
  await expect(password).toHaveCSS('box-shadow', 'rgb(0, 0, 0) 0px 0px 0px 1px inset')
  await password.fill('not-retained')
  await page.getByRole('button', { name: 'Continue' }).click()

  await expect(page.getByRole('heading', { name: 'Ready for the wind' })).toBeVisible()
  await expectCopyAligned()
  await page.getByRole('button', { name: 'Done' }).click()
  await expect(install).toBeFocused()
  await expect(page.getByText('not-retained')).toHaveCount(0)
})

test('demo follows only the fresh-device happy flow through Wi-Fi', async ({ page }) => {
  await page.addInitScript(() => {
    const getContext = HTMLCanvasElement.prototype.getContext
    HTMLCanvasElement.prototype.getContext = function getInstallerDemoContext(type, ...args) {
      if (type === 'webgl' || type === 'webgl2') return null
      return getContext.call(this, type, ...args)
    }
  })
  await page.setViewportSize({ width: 1280, height: 720 })
  await page.goto('/?installerDemo=1')

  const panel = page.getByRole('complementary', { name: 'Windscout settings' })
  await expect(page.getByRole('heading', { name: 'Connect your reTerminal' })).toBeVisible()
  await expect(panel).toHaveCSS('transition-duration', '0.18s')
  const regularHeight = (await panel.boundingBox()).height
  await page.getByRole('button', { name: 'Continue' }).click()
  await expect(page.getByRole('heading', { name: 'Confirm your reTerminal' })).toBeVisible()
  await expect(page.getByText('Make sure this is a reTerminal E1002. Installing will replace its software and saved setup.')).toBeVisible()
  await expect(page.getByRole('button', { name: 'Cancel' })).toHaveCount(0)

  await page.getByRole('button', { name: 'Install Windscout' }).click()
  await expect(page.getByRole('heading', { name: 'Finding Windscout' })).toBeVisible({ timeout: 30_000 })
  await expect(page.getByRole('heading', { name: 'Select a network for Windscout' })).toBeVisible()
  const continueButton = page.getByRole('button', { name: 'Continue' })
  await expect(continueButton).toBeDisabled()
  await expect(continueButton).toHaveCSS('cursor', 'default')
  await expect.poll(async () => {
    const panelBounds = await panel.boundingBox()
    const actionsBounds = await page.locator('.installer-stage__view:not([aria-hidden="true"]) .installer-actions').boundingBox()
    return panelBounds.y + panelBounds.height - actionsBounds.y - actionsBounds.height
  }).toBeCloseTo(12, 0)

  await page.getByRole('combobox', { name: 'Wi-Fi network' }).click()
  await page.getByRole('option', { name: 'Windscout Studio' }).click()
  await page.getByLabel('Password').fill('demo-only')
  const wifiHeight = (await panel.boundingBox()).height
  await page.getByRole('button', { name: 'Continue' }).click()
  await expect(page.getByRole('heading', { name: 'Applying setup' })).toBeVisible()
  await expect(panel).toHaveCSS('transition-delay', '0.16s')
  expect((await panel.boundingBox()).height).toBeLessThanOrEqual(wifiHeight)
  await expect(page.getByRole('heading', { name: 'Ready for the wind' })).toBeVisible()
  await expect.poll(async () => (await panel.boundingBox()).height).toBeCloseTo(regularHeight, 0)
})

test('keeps the installer available at narrow desktop zoom without horizontal overflow', async ({ page }) => {
  await installFakeDevice(page)
  await page.setViewportSize({ width: 640, height: 720 })
  await page.goto('/?configure')
  await page.getByRole('button', { name: 'Install', exact: true }).click()
  await expect(page.getByRole('heading', { name: 'Connect your reTerminal' })).toBeVisible()
  expect(await page.evaluate(() => document.body.scrollWidth)).toBe(640)
})

test('shows a confirmed diagnostic reference without blocking recovery', async ({ page }) => {
  await installFailingDevice(page)
  await page.setViewportSize({ width: 1280, height: 720 })
  await page.goto('/?configure')
  await page.getByRole('button', { name: 'Install', exact: true }).click()
  await page.getByRole('button', { name: 'Continue' }).click()

  await expect(page.getByRole('alert')).toContainText('could not access')
  await expect(page.getByText('Technical details sent')).toBeVisible()
  await expect(page.getByText('Diagnostic reference: WS-TEST123456').first()).toBeVisible()
  await expect(page.getByRole('button', { name: 'Close' })).toBeEnabled()
})

test('grows a Wi-Fi error state so both recovery actions remain usable', async ({ page }) => {
  await installWifiFailure(page)
  await page.setViewportSize({ width: 1280, height: 720 })
  await page.goto('/?configure')
  await page.getByRole('button', { name: 'Install', exact: true }).click()
  await page.getByRole('button', { name: 'Continue' }).click()

  const panel = page.getByRole('complementary', { name: 'Windscout settings' })
  const scanAgain = page.getByRole('button', { name: 'Scan again' })
  const continueButton = page.getByRole('button', { name: 'Continue' })
  await expect(page.getByRole('heading', { name: 'Select a network for Windscout' })).toBeVisible()
  await expect(page.getByText('Diagnostic reference: WS-TEST123456').first()).toBeVisible()
  await expect(scanAgain).toBeVisible()
  await expect(scanAgain).toBeEnabled()

  await page.getByRole('combobox', { name: 'Wi-Fi network' }).click()
  await page.getByRole('option', { name: 'Windscout Studio' }).click()
  await page.getByLabel('Password').fill('layout-only')
  await expect(continueButton).toBeVisible()
  await expect(continueButton).toBeEnabled()

  const [panelBounds, actionsBounds] = await Promise.all([
    panel.boundingBox(),
    page.locator('.installer-stage__view:not([aria-hidden="true"]) .installer-actions').boundingBox(),
  ])
  expect(actionsBounds.y + actionsBounds.height).toBeLessThanOrEqual(panelBounds.y + panelBounds.height - 12)
})

test('keeps the inspector height when the installer opens with threshold hidden or shown', async ({ page }) => {
  await installFakeDevice(page)
  await page.setViewportSize({ width: 1280, height: 720 })
  await page.goto('/?configure')

  const panel = page.getByRole('complementary', { name: 'Windscout settings' })
  const install = page.getByRole('button', { name: 'Install', exact: true })
  const threshold = page.getByRole('switch', { name: 'Wind threshold' })
  const panelHeight = async () => (await panel.boundingBox()).height

  for (const showThreshold of [false, true]) {
    if (showThreshold) await threshold.click()

    const settingsHeight = await panelHeight()
    await install.click()
    await expect(page.getByRole('heading', { name: 'Connect your reTerminal' })).toBeVisible()
    expect(await panelHeight()).toBe(settingsHeight)

    await page.getByRole('button', { name: 'Continue' }).click()
    const confirmation = page.getByRole('button', { name: 'Install Windscout' })
    await expect(confirmation).toBeVisible()
    expect(await panelHeight()).toBe(settingsHeight)

    const panelBounds = await panel.boundingBox()
    const confirmationBounds = await confirmation.boundingBox()
    expect(confirmationBounds.y + confirmationBounds.height).toBeLessThanOrEqual(
      panelBounds.y + panelBounds.height,
    )

    await page.getByRole('button', { name: 'Back to configurator' }).click()
  }
})
