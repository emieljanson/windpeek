import { defineConfig, devices } from '@playwright/test'

const port = Number(process.env.PLAYWRIGHT_PORT || 43_173)
const baseURL = `http://127.0.0.1:${port}`

export default defineConfig({
  testDir: './tests/e2e',
  outputDir: './test-results',
  timeout: process.env.CI ? 90_000 : 60_000,
  fullyParallel: true,
  workers: process.env.CI ? 1 : undefined,
  retries: process.env.CI ? 1 : 0,
  expect: {
    timeout: process.env.CI ? 15_000 : 5_000,
  },
  reporter: 'list',
  use: {
    baseURL,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  webServer: {
    // Browser tests mock the installer firmware responses. Starting Vite
    // directly keeps an unrelated local device build from blocking UI QA.
    command: `vite --host 127.0.0.1 --port ${port}`,
    url: baseURL,
    reuseExistingServer: false,
    env: {
      ...process.env,
      VITE_GEOAPIFY_API_KEY: 'playwright-key',
      VITE_NEARBY_LOCATION_URL: '/__windpeek-location',
    },
  },
  projects: [
    { name: 'chromium', use: { ...devices['Desktop Chrome'] } },
  ],
})
