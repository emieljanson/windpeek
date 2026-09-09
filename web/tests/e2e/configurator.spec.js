import { expect, test } from '@playwright/test'
import { amsterdamDate, forecastResponseForLatitude } from './helpers/forecast'

const CONFIGURATOR_READY_TIMEOUT_MS = 30_000

async function mockForecastApi(page, state = { fail: false, tideUnsupported: false }) {
  const requests = []
  await page.route('https://api.open-meteo.com/v1/forecast**', async (route) => {
    const url = new URL(route.request().url())
    requests.push(url)
    if (state.fail) {
      await route.fulfill({ status: 503, body: 'Unavailable' })
      return
    }
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(
      forecastResponseForLatitude(Number(url.searchParams.get('latitude'))),
    ) })
  })
  await page.route('https://marine-api.open-meteo.com/v1/marine**', async (route) => {
    const firstDate = amsterdamDate()
    const start = Math.floor(Date.parse(`${firstDate}T00:00:00+02:00`) / 1000)
    const times = Array.from({ length: 120 }, (_, index) => start + index * 3600)
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({
        timezone: 'Europe/Amsterdam',
        hourly_units: { time: 'unixtime', sea_level_height_msl: 'm' },
        hourly: {
          time: times,
          sea_level_height_msl: state.tideUnsupported
            ? times.map(() => null)
            : times.map((_, index) => Math.sin(index / 6) * 0.8),
        },
      }),
    })
  })
  return requests
}

async function mockGeoapify(page) {
  const autocompleteRequests = []
  const reverseRequests = []
  await page.route('https://api.geoapify.com/v1/geocode/autocomplete**', async (route) => {
    autocompleteRequests.push(new URL(route.request().url()))
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ results: [{
        place_id: 'hindeloopen-id',
        name: 'Hindeloopen',
        city: 'Hindeloopen',
        state: 'Friesland',
        country: 'Netherlands',
        country_code: 'nl',
        formatted: 'Hindeloopen, Friesland, Netherlands',
        lat: 52.9432,
        lon: 5.4007,
        timezone: { name: 'Europe/Amsterdam' },
      }] }),
    })
  })
  await page.route('https://api.geoapify.com/v1/geocode/reverse**', async (route) => {
    reverseRequests.push(new URL(route.request().url()))
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ results: [{
        place_id: 'confirmed-pin',
        lat: Number(new URL(route.request().url()).searchParams.get('lat')),
        lon: Number(new URL(route.request().url()).searchParams.get('lon')),
        timezone: { name: 'Europe/Amsterdam' },
      }] }),
    })
  })
  await page.route('https://maps.geoapify.com/v1/styles/positron/style.json**', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ version: 8, sources: {}, layers: [] }),
    }))
  return { autocompleteRequests, reverseRequests }
}

function forecastStatus(page) {
  return page.locator('.forecast-status')
}

async function selectWithKeyboard(page, name, search) {
  if (name === 'Wind model') await page.locator('.forecast-advanced > summary').click()
  const control = page.getByRole('combobox', { name, exact: true })
  await control.focus()
  await page.keyboard.press('Enter')
  await page.keyboard.type(search)
  await page.keyboard.press('Enter')
  return control
}

function splitShadows(value) {
  const shadows = []
  let depth = 0
  let start = 0
  for (let index = 0; index < value.length; index += 1) {
    if (value[index] === '(') depth += 1
    if (value[index] === ')') depth -= 1
    if (value[index] === ',' && depth === 0) {
      shadows.push(value.slice(start, index).trim())
      start = index + 1
    }
  }
  shadows.push(value.slice(start).trim())
  return shadows.filter(Boolean)
}

test('keeps the implicit default empty, then shows and restores a chosen spot', async ({ page }) => {
  const requests = await mockForecastApi(page)
  await page.setViewportSize({ width: 1280, height: 900 })
  await page.goto('/?configure')

  await expect(page.getByRole('region', { name: 'Windpeek 3D preview' })).toBeVisible()
  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Brouwersdam', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  expect(requests).toHaveLength(1)

  const spot = page.getByRole('combobox', { name: 'Search spot' })
  await expect(spot).toHaveValue('')
  await spot.click()
  await expect(page.getByRole('listbox')).toHaveCount(0)
  await expect(page.getByText('No existing spots found', { exact: true })).toHaveCount(0)
  await spot.fill('b')
  await expect(page.getByRole('listbox')).toHaveCount(0)
  await page.keyboard.press(process.platform === 'darwin' ? 'Meta+A' : 'Control+A')
  await page.keyboard.type('brouw')
  await expect(page.getByRole('option', { name: 'Brouwersdam', exact: true })).toBeVisible()
  await expect(page.getByRole('option', { name: 'Edam' })).toHaveCount(0)
  await page.keyboard.press('Enter')
  await expect(spot).toHaveValue('Brouwersdam')
  await expect(spot).toBeFocused()
  expect(requests).toHaveLength(1)

  await page.keyboard.press(process.platform === 'darwin' ? 'Meta+A' : 'Control+A')
  await page.keyboard.type('nowhere')
  await expect(page.getByText('No existing spots found', { exact: true })).toHaveCount(0)
  await expect(page.locator('.setting-popup__separator')).toHaveCount(0)
  await page.keyboard.press('Tab')
  await expect(spot).toHaveValue('Brouwersdam')
  expect(requests).toHaveLength(1)
})

test('uses model typeahead and restores focus when its popup is dismissed', async ({ page }) => {
  const requests = await mockForecastApi(page)
  await page.setViewportSize({ width: 1280, height: 900 })
  await page.goto('/?configure')
  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Brouwersdam', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })

  const model = await selectWithKeyboard(page, 'Wind model', 'gfs')
  await expect(model).toContainText('GFS')
  await expect(forecastStatus(page)).toContainText('Live GFS forecast for Brouwersdam')
  await expect(page.locator('.scene-host')).toHaveAttribute('data-forecast-model', 'ncep_gfs_seamless')
  expect(requests).toHaveLength(1)

  await page.keyboard.press('Enter')
  await expect(page.getByRole('option', { name: 'GFS' })).toBeVisible()
  await page.keyboard.press('Escape')
  await expect(model).toBeFocused()
})

test('keeps threshold state explicit and redraws the live preview', async ({ page }) => {
  await mockForecastApi(page)
  await page.setViewportSize({ width: 1280, height: 900 })
  await page.goto('/?configure')
  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  const before = await page.locator('canvas').screenshot()

  await page.locator('.forecast-advanced > summary').click()
  const showThreshold = page.getByRole('switch', { name: 'Wind threshold' })
  await expect(showThreshold).toBeChecked()
  const threshold = page.getByRole('spinbutton', { name: 'Minimum wind' })
  await threshold.click()
  await expect(threshold).toHaveCSS('box-shadow', 'none')
  await page.keyboard.press('Shift+Tab')
  await page.keyboard.press('Tab')
  await expect(threshold).toHaveCSS('box-shadow', /inset/)
  await threshold.fill('24')
  await expect(threshold).toHaveValue('24')
  const withThreshold = await page.locator('canvas').screenshot()
  expect(withThreshold.equals(before)).toBe(false)

  await showThreshold.focus()
  await page.keyboard.press('Space')
  await expect(showThreshold).not.toBeChecked()
  await expect(threshold).toHaveCount(0)
  await page.keyboard.press('Space')
  await expect(page.getByRole('spinbutton', { name: 'Minimum wind' })).toHaveValue('24')
})

test('loads the local CAD model into the constrained 3D scene', async ({ page }) => {
  await mockForecastApi(page)
  await page.setViewportSize({ width: 1200, height: 900 })
  await page.goto('/?configure')

  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await expect(page.getByRole('button', { name: 'Reset view' })).toHaveCount(0)
  await expect(page.getByRole('button', { name: 'Front' })).toHaveCount(0)
  await expect(page.getByTestId('install-continuation')).toBeVisible()
})

test('sizes the 3D canvas sharply while the CAD model is still loading', async ({ page }) => {
  await mockForecastApi(page)
  await page.emulateMedia({ colorScheme: 'dark' })
  await page.setViewportSize({ width: 1200, height: 900 })

  let releaseModel
  const modelBlocked = new Promise((resolve) => { releaseModel = resolve })
  await page.route('**/devices/e1002/e1002.glb', async (route) => {
    await modelBlocked
    await route.continue()
  })

  await page.goto('/?configure', { waitUntil: 'domcontentloaded' })
  const canvas = page.locator('canvas')
  await expect(canvas).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await expect(page.locator('.scene-host')).toHaveAttribute('data-scene-status', 'loading')
  await expect(page.locator('.scene-status')).toHaveCSS('color', 'rgb(170, 170, 172)')

  const dimensions = await canvas.evaluate((element) => ({
    width: element.width,
    height: element.height,
    clientWidth: element.clientWidth,
    clientHeight: element.clientHeight,
    pixelRatio: Math.min(window.devicePixelRatio, 2),
  }))
  expect(dimensions.width).toBe(dimensions.clientWidth * dimensions.pixelRatio)
  expect(dimensions.height).toBe(dimensions.clientHeight * dimensions.pixelRatio)

  releaseModel()
  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
})

test('switches the live preview to another supported spot without a page reload', async ({ page }) => {
  const requests = await mockForecastApi(page)
  await page.setViewportSize({ width: 1200, height: 900 })
  await page.goto('/?configure')
  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Brouwersdam', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })

  const spot = page.getByRole('combobox', { name: 'Search spot' })
  await spot.focus()
  await page.keyboard.press(process.platform === 'darwin' ? 'Meta+A' : 'Control+A')
  await page.keyboard.type('eda')
  await page.keyboard.press('Enter')

  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Edam', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await expect(page.locator('.scene-host')).toHaveAttribute('data-forecast-spot', 'edam')
  await expect(page.getByTestId('forecast-label')).toHaveCount(0)
  expect(requests).toHaveLength(2)
  expect(requests[1].searchParams.get('latitude')).toBe('52.512600')
})

test('creates and remembers a personal spot only after the explicit map flow', async ({ page }) => {
  const forecastRequests = await mockForecastApi(page)
  const { autocompleteRequests, reverseRequests } = await mockGeoapify(page)
  await page.setViewportSize({ width: 1280, height: 900 })
  await page.goto('/?configure')
  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Brouwersdam', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })

  const spot = page.getByRole('combobox', { name: 'Search spot' })
  await spot.fill('Windpeek Test Bay')
  const addTestBay = page.getByRole('option', { name: 'Add Windpeek Test Bay' })
  await expect(addTestBay).toBeVisible()
  expect(autocompleteRequests).toHaveLength(0)
  await addTestBay.click()

  const dialog = page.getByRole('dialog', { name: 'Add spot' })
  await expect(dialog).toBeVisible()
  const placeSearch = dialog.getByRole('combobox', { name: 'Search for a place' })
  await expect(placeSearch).toBeFocused()
  await expect(dialog.locator('.maplibregl-canvas')).toBeVisible()
  await expect(dialog.locator('.maplibregl-ctrl-attrib')).not.toHaveClass(/maplibregl-compact-show/)
  await expect(placeSearch).toHaveValue('Hindeloopen')
  await expect(dialog.getByRole('button', { name: 'Add Hindeloopen' })).toBeEnabled()
  await expect(dialog.locator('.spot-dialog__search-field')).toHaveCSS('height', '38px')
  expect(autocompleteRequests).toHaveLength(1)
  expect(autocompleteRequests[0].searchParams.get('type')).toBe('locality')
  expect(autocompleteRequests[0].searchParams.get('bias')).toBe('proximity:5.3,52.2')

  await expect(dialog.getByText('Move the map until the pin is on your spot by the water')).toBeVisible()
  await expect(dialog.locator('.spot-dialog__pin')).toBeVisible()
  await expect(dialog.locator('.maplibregl-canvas')).toBeVisible()
  const mapLayout = await dialog.locator('.spot-dialog__map').evaluate((element) => ({
    height: element.getBoundingClientRect().height,
    parentHeight: element.parentElement.getBoundingClientRect().height,
    position: getComputedStyle(element).position,
  }))
  expect(mapLayout.position).toBe('absolute')
  expect(mapLayout.height).toBe(mapLayout.parentHeight)
  await dialog.getByRole('button', { name: 'Add Hindeloopen' }).click()

  await expect(dialog).toHaveCount(0)
  await expect(spot).toHaveValue('Hindeloopen')
  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Hindeloopen', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  expect(reverseRequests).toHaveLength(1)
  expect(forecastRequests.at(-1).searchParams.get('latitude')).toBe('52.943200')

  await page.reload()
  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Hindeloopen', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await page.getByRole('combobox', { name: 'Search spot' }).fill('Hind')
  await expect(page.getByRole('option', { name: 'Hindeloopen', exact: true })).toBeVisible()
})

test('keeps compact mode focused on direct display options', async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'userAgent', { configurable: true, value: 'iPhone' })
  })
  const requests = await mockForecastApi(page)
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/?configure')
  await expect(forecastStatus(page)).toContainText('Live Best Match forecast for Brouwersdam', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })

  await expect(page.locator('.mobile-settings-sheet')).toHaveCount(0)
  await expect(page.getByRole('combobox', { name: 'Search spot' })).toHaveCount(0)
  await expect(page.getByRole('dialog', { name: 'Add spot' })).toHaveCount(0)
  await expect(page.getByRole('button', { name: 'About reTerminal devices' })).toHaveCount(0)
  await expect(page.getByRole('combobox', { name: 'reTerminal' })).toHaveCount(0)
  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await expect(page.getByRole('combobox', { name: 'Wind', exact: true })).toBeVisible()
  await expect(page.getByRole('combobox', { name: 'Waves', exact: true })).toBeVisible()
  await expect(page.getByRole('switch', { name: 'Temperature', exact: true })).toBeChecked()
  await page.locator('.forecast-advanced > summary').click()
  const threshold = page.getByRole('switch', { name: 'Wind threshold' })
  await expect(threshold).toBeChecked()
  await threshold.press('Space')
  await expect(threshold).not.toBeChecked()
  expect(requests).toHaveLength(1)
})

test('recomposes the preview when Weather, Temperature, and Tide change', async ({ page }) => {
  await mockForecastApi(page)
  await page.setViewportSize({ width: 1200, height: 900 })
  await page.goto('/?configure')
  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  const before = await page.locator('canvas').screenshot()

  const weather = page.getByRole('switch', { name: 'Weather' })
  await weather.focus()
  await page.keyboard.press('Space')
  await expect(weather).not.toBeChecked()

  await page.locator('.forecast-advanced > summary').click()
  const temperature = page.getByRole('radio', { name: '°F', exact: true })
  await temperature.click()
  await expect(temperature).toBeChecked()

  const tide = page.getByRole('switch', { name: 'Tide' })
  await expect(tide).toBeEnabled()
  await expect(tide).toBeChecked()
  await tide.focus()
  await page.keyboard.press('Space')
  await expect(tide).not.toBeChecked()
  const after = await page.locator('canvas').screenshot()
  expect(after.equals(before)).toBe(false)
})

test('keeps unavailable Tide on Hide and explains it in a tooltip', async ({ page }) => {
  await mockForecastApi(page, { tideUnsupported: true })
  await page.setViewportSize({ width: 1280, height: 900 })
  await page.goto('/?configure')

  const tide = page.getByRole('switch', { name: 'Tide' })
  await expect(tide).toBeDisabled({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await expect(tide).not.toBeChecked()
  await expect(tide).not.toHaveAttribute('disabled', '')
  await expect(tide).toHaveAttribute('aria-disabled', 'true')
  await expect(page.locator('.tide-capability-message')).toHaveCount(0)

  const tideLabel = page.getByText('Tide', { exact: true })
  await expect(tideLabel).toHaveCSS('color', 'rgb(148, 148, 150)')
  await expect(tideLabel.locator('..').locator('..')).toHaveCSS('opacity', '1')

  const tooltip = page.getByRole('tooltip')
  await expect(tooltip).toBeHidden()
  await tide.hover()
  await expect(tooltip).toBeVisible()
  await expect(tooltip).toContainText('not available')
  await expect(tooltip).toHaveCSS('background-color', 'rgb(0, 0, 0)')
  await expect(tooltip).toHaveCSS('color', 'rgb(255, 255, 255)')
  await expect(tooltip).toHaveCSS('font-size', '10px')

  await tide.focus()
  await expect(tooltip).toBeVisible()
  await page.keyboard.press('Space')
  await expect(tide).not.toBeChecked()
})

for (const viewport of [
  { name: 'desktop', width: 1280, height: 900, radius: '16px' },
]) {
  test(`keeps the floating panel and popups polished at ${viewport.name} width`, async ({ page }) => {
    await mockForecastApi(page)
    await page.setViewportSize({ width: viewport.width, height: viewport.height })
    await page.emulateMedia({ reducedMotion: 'reduce' })
    await page.goto('/?configure')
    await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })

    const panel = page.locator('.settings-panel')
    await expect(panel).toHaveCSS('border-top-width', '0px')
    await expect(panel).toHaveCSS('border-radius', viewport.radius)
    const panelMetrics = await panel.evaluate((element) => {
      const rect = element.getBoundingClientRect()
      const style = getComputedStyle(element)
      return {
        left: rect.left,
        right: rect.right,
        top: rect.top,
        bottom: rect.bottom,
        shadow: style.boxShadow,
        documentWidth: document.documentElement.scrollWidth,
      }
    })
    expect(panelMetrics.left).toBeGreaterThanOrEqual(0)
    expect(panelMetrics.right).toBeLessThanOrEqual(viewport.width)
    expect(panelMetrics.top).toBeGreaterThanOrEqual(0)
    expect(panelMetrics.bottom).toBeLessThanOrEqual(viewport.height)
    expect(panelMetrics.documentWidth).toBeLessThanOrEqual(viewport.width)
    expect(splitShadows(panelMetrics.shadow)).toHaveLength(3)
    expect(panelMetrics.bottom - panelMetrics.top).toBeLessThanOrEqual(420)

    await expect(page.locator('.inspector-rows')).toHaveCSS('row-gap', '8px')

    const dividerBox = await page.locator('.inspector-divider').boundingBox()
    expect(Math.abs(dividerBox.x - panelMetrics.left)).toBeLessThanOrEqual(1)
    expect(Math.abs(dividerBox.x + dividerBox.width - panelMetrics.right)).toBeLessThanOrEqual(1)

    const initialSearch = page.getByRole('combobox', { name: 'Search spot' })
    await expect(initialSearch).toBeFocused()
    await expect(initialSearch).toHaveValue('')
    await expect(page.getByRole('listbox')).toHaveCount(0)
    const initialSearchStyle = await initialSearch.evaluate((element) => {
      const style = getComputedStyle(element)
      return { boxShadow: style.boxShadow, caretColor: style.caretColor }
    })
    expect(initialSearchStyle).toEqual({ boxShadow: 'none', caretColor: 'rgb(0, 0, 0)' })
    const installButton = page.getByRole('button', { name: 'Install' })
    const [searchBox, installBox] = await Promise.all([
      initialSearch.boundingBox(),
      installButton.boundingBox(),
    ])
    expect(searchBox.height).toBe(installBox.height)

    const rowMetrics = await page.locator('.setting-row:visible').evaluateAll((rows) => rows.map((row) => {
      const label = row.querySelector('.setting-row__label').getBoundingClientRect()
      const control = row.querySelector('.setting-row__control').getBoundingClientRect()
      return { labelLeft: Math.round(label.left), controlLeft: Math.round(control.left) }
    }))
    expect(new Set(rowMetrics.map(({ labelLeft }) => labelLeft)).size).toBe(1)
    expect(new Set(rowMetrics.map(({ controlLeft }) => controlLeft)).size).toBe(1)
    const controlHeights = await page.locator('.setting-control:visible').evaluateAll((controls) =>
      controls.map((control) => ({
        height: control.getBoundingClientRect().height,
        search: control.closest('.inspector-search') != null,
      })))
    expect(controlHeights.filter(({ search }) => !search)
      .every(({ height }) => Math.abs(height - 32) < 0.5)).toBe(true)
    expect(controlHeights.filter(({ search }) => search)
      .every(({ height }) => Math.abs(height - 38) < 0.5)).toBe(true)

    const temperature = page.getByRole('switch', { name: 'Temperature', exact: true })
    await expect(temperature).toBeChecked()
    const weatherHide = page.getByRole('switch', { name: 'Weather' }).locator('.setting-switch__segment--off')
    await expect(weatherHide).toHaveCSS('color', 'rgb(148, 148, 150)')
    await weatherHide.hover()
    await expect(weatherHide).toHaveCSS('color', 'rgb(0, 0, 0)')

    await page.locator('.forecast-advanced > summary').click()
    const model = page.getByRole('combobox', { name: 'Wind model', exact: true })
    const triggerBox = await model.boundingBox()
    const panelBeforeMenu = await panel.boundingBox()
    const bodyOverflowBeforeMenu = await page.locator('body').evaluate((element) => getComputedStyle(element).overflow)
    await model.click()
    await expect(page.locator('.setting-select__trigger').first()).toHaveCSS('box-shadow', 'none')
    const popup = page.getByRole('listbox')
    await expect(popup).toBeVisible()
    await expect(popup.locator('.setting-popup__viewport')).toHaveCSS('padding-top', '4px')
    const selectedOption = page.getByRole('option', { name: 'Best Match' })
    await expect(selectedOption).toHaveCSS('font-weight', '500')
    await expect(selectedOption).toHaveCSS('border-radius', '8px')
    const popupBox = await popup.boundingBox()
    expect(popupBox.x).toBeGreaterThanOrEqual(0)
    expect(popupBox.x + popupBox.width).toBeLessThanOrEqual(viewport.width)
    expect(popupBox.width).toBeGreaterThanOrEqual(triggerBox.width - 1)
    await expect.poll(async () => {
      const selectedOptionBox = await page.getByRole('option', { name: 'Best Match' }).boundingBox()
      return Math.abs(selectedOptionBox.y - triggerBox.y)
    }).toBeLessThanOrEqual(1)
    expect(await panel.boundingBox()).toEqual(panelBeforeMenu)
    await expect(page.locator('body')).toHaveCSS('overflow', bodyOverflowBeforeMenu)
    await page.evaluate(() => {
      window.__dropdownCloseFrames = []
      let remainingFrames = 8
      const sampleGeometry = () => {
        window.__dropdownCloseFrames.push(Array.from(document.querySelectorAll('.setting-row')).map((row) => {
          const rect = row.getBoundingClientRect()
          const label = row.querySelector('.setting-row__label').getBoundingClientRect()
          const control = row.querySelector('.setting-row__control').getBoundingClientRect()
          return {
            top: rect.top,
            height: rect.height,
            labelTop: label.top,
            labelHeight: label.height,
            controlTop: control.top,
            controlHeight: control.height,
          }
        }))
        remainingFrames -= 1
        if (remainingFrames > 0) requestAnimationFrame(sampleGeometry)
      }
      requestAnimationFrame(sampleGeometry)
    })
    await page.keyboard.press('Escape')
    await expect(model).toBeFocused()
    await expect.poll(() => page.evaluate(() => window.__dropdownCloseFrames.length)).toBe(8)
    const closeFrames = await page.evaluate(() => window.__dropdownCloseFrames)
    const uniqueCloseFrames = [...new Set(closeFrames.map((frame) => JSON.stringify(frame)))]
    expect(uniqueCloseFrames, JSON.stringify(uniqueCloseFrames)).toHaveLength(1)
    const focusStyle = await model.evaluate((element) => {
      const style = getComputedStyle(element)
      return { outlineStyle: style.outlineStyle, boxShadow: style.boxShadow }
    })
    expect(focusStyle.outlineStyle).toBe('none')
    expect(focusStyle.boxShadow).toBe('rgb(0, 0, 0) 0px 0px 0px 1px inset')

    await page.keyboard.press('Shift+Tab')
    await expect(page.getByRole('button', { name: 'About wind models' })).toBeFocused()
    await initialSearch.focus()
    await expect(initialSearch).toHaveCSS('box-shadow', 'rgb(0, 0, 0) 0px 0px 0px 1px inset')

    const spot = initialSearch
    await spot.fill('Bro')
    const spotPopup = page.getByRole('listbox')
    await expect(spotPopup).toBeVisible()
    const spotTriggerWidth = await spotPopup.evaluate((element) => {
      const value = getComputedStyle(element).getPropertyValue('--reka-combobox-trigger-width')
      return Number.parseFloat(value)
    })
    const spotPopupBox = await spotPopup.boundingBox()
    expect(spotPopupBox.x).toBeGreaterThanOrEqual(0)
    expect(spotPopupBox.x + spotPopupBox.width).toBeLessThanOrEqual(viewport.width)
    expect(Math.abs(spotPopupBox.width - spotTriggerWidth)).toBeLessThanOrEqual(1)
    const spotBox = await spot.boundingBox()
    const popupGaps = [
      Math.abs(spotPopupBox.y - (spotBox.y + spotBox.height + 4)),
      Math.abs(spotBox.y - (spotPopupBox.y + spotPopupBox.height + 4)),
    ]
    expect(Math.min(...popupGaps)).toBeLessThanOrEqual(1)

    const longQuery = 'A deliberately very long imaginary spot name beside the water'
    await spot.fill(longQuery)
    const longAction = page.getByRole('option', { name: `Add ${longQuery}` })
    await expect(longAction).toBeVisible()
    const truncation = await longAction.locator('span').last().evaluate((label) => {
      const style = getComputedStyle(label)
      return {
        overflow: style.overflow,
        textOverflow: style.textOverflow,
        whiteSpace: style.whiteSpace,
        isClipped: label.scrollWidth > label.clientWidth,
      }
    })
    expect(truncation).toEqual({
      overflow: 'hidden',
      textOverflow: 'ellipsis',
      whiteSpace: 'nowrap',
      isClipped: true,
    })
    const longPopupBox = await page.getByRole('listbox').boundingBox()
    expect(Math.abs(longPopupBox.width - spotTriggerWidth)).toBeLessThanOrEqual(1)
    await page.keyboard.press('Escape')
    await expect(spot).toBeFocused()
  })
}

test('keeps the compact inspector above the viewport edge without widening the page', async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'userAgent', { configurable: true, value: 'iPhone' })
  })
  await mockForecastApi(page)
  await page.setViewportSize({ width: 390, height: 844 })
  await page.emulateMedia({ reducedMotion: 'reduce' })
  await page.goto('/?configure')
  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })

  const panel = page.locator('.settings-panel--compact')
  const scene = page.locator('.scene-host')
  await expect(panel).toHaveCSS('position', 'fixed')

  const geometry = await page.evaluate(() => {
    const width = document.documentElement.clientWidth
    const offenders = [...document.querySelectorAll('body *')].filter((element) => {
      const rect = element.getBoundingClientRect()
      if (rect.width <= 1 || getComputedStyle(element).position === 'absolute') return false
      return rect.left < -0.5 || rect.right > width + 0.5
    }).map((element) => ({ tag: element.tagName, className: element.className }))
    return {
      body: [document.body.clientWidth, document.body.scrollWidth],
      document: [document.documentElement.clientWidth, document.documentElement.scrollWidth],
      scroll: [window.scrollX, window.scrollY],
      panel: (() => {
        const rect = document.querySelector('.settings-panel--compact').getBoundingClientRect()
        return [rect.left, innerWidth - rect.right, innerHeight - rect.bottom]
      })(),
      scene: (() => {
        const rect = document.querySelector('.scene-host').getBoundingClientRect()
        return [rect.width, rect.height]
      })(),
      offenders,
    }
  })
  const { panel: panelGeometry, ...pageGeometry } = geometry
  expect(pageGeometry).toEqual({
    body: [390, 390],
    document: [390, 390],
    scroll: [0, 0],
    scene: [390, 844],
    offenders: [],
  })
  expect(Math.abs(panelGeometry[0] - panelGeometry[1])).toBeLessThanOrEqual(1)
  expect(panelGeometry[2]).toBe(32)
  const fades = await page.evaluate(() => ({
    top: getComputedStyle(document.querySelector('.product-stage'), '::before').backgroundImage,
    bottom: getComputedStyle(document.querySelector('.configurator-layout'), '::after').backgroundImage,
  }))
  expect(fades.top).toContain('linear-gradient')
  expect(fades.bottom).toContain('linear-gradient')

  await page.setViewportSize({ width: 390, height: 700 })
  await expect.poll(() => panel.evaluate((element) => {
    const rect = element.getBoundingClientRect()
    return Math.round(innerHeight - rect.bottom)
  })).toBe(32)
  await expect.poll(() => scene.evaluate((element) => Math.round(element.getBoundingClientRect().height))).toBe(700)

  await expect(page.getByTestId('install-continuation')).toHaveCount(0)
  await expect(page.getByRole('combobox', { name: 'Search spot' })).toHaveCount(0)
  await expect(page.locator('select[name="model"]')).toHaveCount(0)
  await expect(page.locator('select[name="temperature"]')).toHaveCount(0)
  const weather = page.getByRole('switch', { name: 'Weather', exact: true })
  await weather.locator('.setting-switch__segment--off').click()
  await expect(weather).not.toBeChecked()
  await weather.focus()
  await page.keyboard.press('Tab')
  const temperature = page.getByRole('switch', { name: 'Temperature', exact: true })
  await expect(temperature).toBeFocused()
  await expect(panel).toHaveCSS('border-radius', '24px')

  await page.setViewportSize({ width: 844, height: 390 })
  await expect(panel).toHaveCSS('position', 'fixed')
  await expect(page.getByTestId('install-continuation')).toHaveCount(0)
  await expect(page.getByRole('combobox', { name: 'Search spot' })).toHaveCount(0)
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(844)
})

test('keeps the full installer floating above the model on a narrow desktop', async ({ page }) => {
  await mockForecastApi(page)
  await page.setViewportSize({ width: 844, height: 390 })
  await page.emulateMedia({ reducedMotion: 'reduce' })
  await page.goto('/?installerDemo=1')
  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await expect(page.getByRole('heading', { name: 'Connect your reTerminal' })).toBeVisible()

  const panel = page.locator('.settings-panel')
  const stage = page.locator('.product-stage')
  await expect(page.locator('.settings-panel--compact')).toHaveCount(0)
  await expect(panel).toHaveCSS('position', 'absolute')
  const [panelBox, stageBox] = await Promise.all([panel.boundingBox(), stage.boundingBox()])
  expect(panelBox.y).toBeGreaterThanOrEqual(stageBox.y)
  expect(panelBox.y + panelBox.height).toBeLessThanOrEqual(stageBox.y + stageBox.height)
  expect(panelBox.x + panelBox.width).toBeLessThanOrEqual(stageBox.x + stageBox.width)
  expect(await page.evaluate(() => ({
    scroll: [window.scrollX, window.scrollY],
    document: [document.documentElement.scrollWidth, document.documentElement.scrollHeight],
  }))).toEqual({ scroll: [0, 0], document: [844, 390] })
})

test('keeps the full settings panel below the model inside one continuous 3D scene', async ({ page }) => {
  await mockForecastApi(page)
  await page.setViewportSize({ width: 823, height: 968 })
  await page.goto('/?configure')
  await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })

  const panel = page.locator('.settings-panel')
  const stage = page.locator('.product-stage')
  await expect(page.locator('.settings-panel--compact')).toHaveCount(0)
  await expect(panel).toHaveCSS('position', 'absolute')

  const [panelBox, stageBox, sceneBox] = await Promise.all([
    panel.boundingBox(),
    stage.boundingBox(),
    page.locator('.scene-host').boundingBox(),
  ])
  expect(sceneBox).toEqual(stageBox)
  expect(stageBox).toEqual({ x: 0, y: 0, width: 823, height: 968 })
  expect(panelBox.y).toBeGreaterThan(968 / 2)
  expect(Math.abs(panelBox.x - (823 - panelBox.x - panelBox.width))).toBeLessThanOrEqual(1)
  expect(968 - panelBox.y - panelBox.height).toBe(16)
  expect(await page.evaluate(() => ({
    horizontal: [document.documentElement.clientWidth, document.documentElement.scrollWidth],
    vertical: [document.documentElement.clientHeight, document.documentElement.scrollHeight],
  }))).toEqual({ horizontal: [823, 823], vertical: [968, 968] })
})

for (const viewport of [
  { width: 320, height: 700, compact: false, installer: true },
  { width: 640, height: 700, compact: false, installer: true },
  { width: 896, height: 700, compact: false, installer: true },
  { width: 897, height: 700, compact: false, installer: true },
]) {
  test(`keeps the full desktop surface at ${viewport.width}px`, async ({ page }) => {
    await mockForecastApi(page)
    await page.setViewportSize({ width: viewport.width, height: viewport.height })
    await page.goto('/?configure')
    await expect(page.locator('[data-scene-status="ready"]')).toBeVisible({ timeout: CONFIGURATOR_READY_TIMEOUT_MS })
    await expect(page.locator('.settings-panel--compact')).toHaveCount(viewport.compact ? 1 : 0)
    await expect(page.getByTestId('install-continuation')).toHaveCount(viewport.installer ? 1 : 0)
    await expect(page.getByRole('combobox', { name: 'Search spot' })).toHaveCount(viewport.compact ? 0 : 1)
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(viewport.width)
  })
}

test('labels a first network failure as demo outside the device screen', async ({ page }) => {
  await mockForecastApi(page, { fail: true })
  await page.setViewportSize({ width: 1200, height: 900 })
  await page.goto('/?configure')

  const toast = page.locator('[data-sonner-toast]')
  await expect(toast).toContainText('Live forecast unavailable. Showing demo data.', { timeout: CONFIGURATOR_READY_TIMEOUT_MS })
  await expect(page.getByTestId('forecast-label')).toHaveText('Demo')
  await expect(forecastStatus(page)).toContainText('Live forecast unavailable. Showing demo data.')
  await expect(forecastStatus(page)).toHaveClass(/is-visually-hidden/)
  await expect(page.getByTestId('forecast-label').locator('xpath=ancestor::aside')).toHaveCount(1)
  const toaster = page.locator('[data-sonner-toaster]')
  await expect(toaster).toHaveAttribute('data-x-position', 'right')
  await expect(toaster).toHaveAttribute('data-y-position', 'bottom')
})

test('keeps installation continuation and omits retired controls', async ({ page }) => {
  await mockForecastApi(page)
  await page.setViewportSize({ width: 1280, height: 900 })
  await page.goto('/?configure')

  await expect(page.getByText('Treatment', { exact: true })).toHaveCount(0)
  await expect(page.getByText('Time format', { exact: true })).toHaveCount(0)
  await page.getByTestId('install-continuation').click()
  await expect(page.getByRole('heading', { name: 'Connect your reTerminal' })).toBeVisible()
  await expect(page.getByText(/Connect your reTerminal E1002/)).toBeVisible()
})
