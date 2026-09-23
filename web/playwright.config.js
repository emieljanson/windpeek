import { defineConfig, devices } from '@playwright/test'

const port = Number(process.env.PLAYWRIGHT_PORT || 43_173)
const baseURL = `http://127.0.0.1:${port}`

export default defineConfig({
  testDir: './tests/e2e',
  outputDir: './test-results',
  timeout: process.env.CI ? 90_000 : 60_000,
  fullyParallel: true,
  // WebGL scenes compete for the same GPU even in separate browser contexts.
  workers: process.env.CI ? 1 : 2,
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
    // Warm all dependencies before tests: late optimization reloads active pages.
    // Dev mode retains the deliberately development-only fake USB sessions.
    command: `vite optimize --force && vite --host 127.0.0.1 --port ${port} --strictPort`,
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
