import { expect, test } from '@playwright/test'
import { forecastResponseForLatitude } from './helpers/forecast'

const CONFIGURATOR_READY_TIMEOUT_MS = 30_000
const BROUWERSDAM = { latitude: '51.750600', longitude: '3.857700' }
const EDAM = { latitude: 52.5126, longitude: 5.0486 }

async function mockWeather(page) {
  const forecastRequests = []
  await page.route('https://api.open-meteo.com/v1/forecast**', async (route) => {
    const url = new URL(route.request().url())
    forecastRequests.push(url)
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(forecastResponseForLatitude(Number(url.searchParams.get('latitude')))),
    })
  })
  await page.route('https://marine-api.open-meteo.com/v1/marine**', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({
        timezone: 'Europe/Amsterdam',
        hourly_units: { time: 'unixtime', sea_level_height_msl: 'm' },
        hourly: { time: [], sea_level_height_msl: [] },
      }),
    })
  })
  return forecastRequests
}

async function locationGate(page) {
  let release
  let markRequested
  const requested = new Promise((resolve) => { markRequested = resolve })
  await page.route('**/__windscout-location', async (route) => {
    markRequested()
    await new Promise((releaseRequest) => { release = releaseRequest })
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(EDAM),
    })
  })
  return { requested, release: () => release?.() }
}

test('homepage shows Brouwersdam immediately, then requests the nearby forecast', async ({ page }) => {
  const forecastRequests = await mockWeather(page)
  const location = await locationGate(page)

  await page.goto('/')
  await location.requested
  await expect.poll(() => forecastRequests.length).toBe(1)
  expect(forecastRequests[0].searchParams.get('latitude')).toBe(BROUWERSDAM.latitude)
  expect(forecastRequests[0].searchParams.get('longitude')).toBe(BROUWERSDAM.longitude)
  const hero = page.locator('.landing-hero')
  await expect(hero).toHaveAttribute('data-forecast-spot', 'brouwersdam')
  await expect.poll(async () => Number(await hero.getAttribute('data-forecast-revision'))).toBeGreaterThan(0)
  const initialFrame = await hero.locator('canvas').screenshot()

  location.release()
  await expect.poll(() => forecastRequests.length).toBe(2)
  expect(forecastRequests[1].searchParams.get('latitude')).toBe(EDAM.latitude.toFixed(6))
  expect(forecastRequests[1].searchParams.get('longitude')).toBe(EDAM.longitude.toFixed(6))
  await expect(hero).toHaveAttribute('data-forecast-spot', 'edam')
  await expect.poll(async () => {
    const currentFrame = await hero.locator('canvas').screenshot()
    return currentFrame.equals(initialFrame)
  }).toBe(false)
})

test('nearby configurator spot does not fill the search field', async ({ page }) => {
  await mockWeather(page)
  await page.route('**/__windscout-location', (route) => route.fulfill({
    status: 200,
    contentType: 'application/json',
    body: JSON.stringify(EDAM),
  }))

  await page.goto('/?configure')
  await expect(page.locator('.scene-host')).toHaveAttribute(
    'data-forecast-spot',
    'edam',
    { timeout: CONFIGURATOR_READY_TIMEOUT_MS },
  )
  await expect(page.getByRole('combobox', { name: 'Search spot' })).toHaveValue('')
})

test('failed location lookup quietly keeps Brouwersdam', async ({ page }) => {
  await mockWeather(page)
  let locationRequested
  const requestSeen = new Promise((resolve) => { locationRequested = resolve })
  await page.route('**/__windscout-location', async (route) => {
    locationRequested()
    await route.fulfill({ status: 503, body: '' })
  })

  await page.goto('/?configure')
  await requestSeen
  await expect(page.locator('.scene-host')).toHaveAttribute(
    'data-forecast-spot',
    'brouwersdam',
    { timeout: CONFIGURATOR_READY_TIMEOUT_MS },
  )
  await expect(page.getByRole('alert')).toHaveCount(0)
})

test('typing before a delayed location response keeps the user in control', async ({ page }) => {
  await mockWeather(page)
  const location = await locationGate(page)

  await page.goto('/?configure')
  await location.requested
  const spotSearch = page.getByRole('combobox', { name: 'Search spot' })
  await spotSearch.fill('eda')
  location.release()

  await expect(page.locator('.configurator-page')).toHaveAttribute(
    'data-nearby-default-status',
    'ignored',
  )
  await expect(spotSearch).toHaveValue('eda')
  await expect(page.locator('.scene-host')).toHaveAttribute(
    'data-forecast-spot',
    'brouwersdam',
    { timeout: CONFIGURATOR_READY_TIMEOUT_MS },
  )
})
