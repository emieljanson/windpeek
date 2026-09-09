import { chromium } from '@playwright/test'
import { createServer } from 'vite'
import { mkdir, writeFile } from 'node:fs/promises'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const webRoot = join(dirname(fileURLToPath(import.meta.url)), '..')
const outputDirectory = join(webRoot, 'public', 'devices', 'previews')
const targets = new Set(process.argv.slice(2))
const devices = Object.freeze([
  ['e1001', 'seeedstudio_reterminal_e1001'],
  ['e1002', 'seeedstudio_reterminal_e1002'],
  ['e1003', 'seeedstudio_reterminal_e1003'],
])

await mkdir(outputDirectory, { recursive: true })

const server = await createServer({
  root: webRoot,
  logLevel: 'error',
  server: { host: '127.0.0.1', port: 0 },
})

let browser
try {
  await server.listen()
  const baseUrl = server.resolvedUrls?.local?.[0]
  if (!baseUrl) throw new Error('Vite did not publish a local preview URL')

  browser = await chromium.launch({ headless: true })
  const page = await browser.newPage({
    viewport: { width: 720, height: 520 },
    deviceScaleFactor: 2,
    reducedMotion: 'reduce',
  })

  for (const variant of ['wind', 'swell']) {
    for (const [filename, boardId] of devices) {
      if (targets.size && !targets.has(`${filename}-${variant}`)) continue
      await page.goto(`${baseUrl}?devicePreview=${boardId}&site=${variant}`, { waitUntil: 'networkidle' })
      const scene = page.locator('.scene-host')
      await scene.waitFor({ state: 'visible' })
      await page.waitForFunction(() => (
        document.querySelector('.scene-host')?.dataset.sceneStatus === 'ready'
      ))
      await page.evaluate(async ({ variant, boardId }) => {
        const { useConfiguratorStore } = await import('/src/stores/configurator.js')
        const { siteDisplayDefaults } = await import('/src/marketing/siteVariant.js')
        const { brouwersdamForecast, brouwersdamTide } = await import('/src/fixtures/brouwersdam.js')
        const { brouwersdamSwell } = await import('/src/fixtures/brouwersdamSwell.js')
        useConfiguratorStore().$patch(state => Object.assign(state, {
          ...siteDisplayDefaults({ id: variant }),
          ...(variant === 'swell' && boardId === 'seeedstudio_reterminal_e1003' ? { windSize: 'large' } : {}),
          forecast: brouwersdamForecast,
          tide: brouwersdamTide, tideStatus: 'available',
          swell: variant === 'swell' ? brouwersdamSwell : null,
          swellStatus: variant === 'swell' ? 'ready' : 'idle',
          swellFocus: variant === 'swell',
        }))
      }, { variant, boardId })
      await page.evaluate(() => new Promise((resolve) => {
        requestAnimationFrame(() => requestAnimationFrame(resolve))
      }))

      const dataUrl = await scene.locator('canvas').evaluate((canvas) => {
        const source = document.createElement('canvas')
        source.width = canvas.width
        source.height = canvas.height
        const sourceContext = source.getContext('2d')
        sourceContext.drawImage(canvas, 0, 0)

        const pixels = sourceContext.getImageData(0, 0, source.width, source.height).data
        let minX = source.width
        let minY = source.height
        let maxX = -1
        let maxY = -1
        let solidMinX = source.width
        let solidMaxX = -1
        for (let y = 0; y < source.height; y += 1) {
          for (let x = 0; x < source.width; x += 1) {
            const alpha = pixels[(y * source.width + x) * 4 + 3]
            if (alpha < 2) continue
            minX = Math.min(minX, x)
            minY = Math.min(minY, y)
            maxX = Math.max(maxX, x)
            maxY = Math.max(maxY, y)
            if (alpha >= 96) {
              solidMinX = Math.min(solidMinX, x)
              solidMaxX = Math.max(solidMaxX, x)
            }
          }
        }
        if (maxX < minX || maxY < minY) throw new Error('The device render is fully transparent')
        if (minX === 0 && minY === 0 && maxX === source.width - 1 && maxY === source.height - 1) {
          throw new Error('The device render has an opaque background')
        }

        const padding = 24
        const contentWidth = maxX - minX + 1
        const contentCenterX = (minX + maxX + 1) / 2
        const deviceCenterX = solidMaxX >= solidMinX
          ? (solidMinX + solidMaxX + 1) / 2
          : contentCenterX
        const outputWidth = Math.min(
          source.width,
          contentWidth + padding * 2 + Math.ceil(Math.abs(deviceCenterX - contentCenterX) * 2),
        )
        const left = Math.max(0, Math.min(
          source.width - outputWidth,
          Math.round(deviceCenterX - outputWidth / 2),
        ))
        const top = Math.max(0, minY - padding)
        const right = left + outputWidth
        const bottom = Math.min(source.height, maxY + padding + 1)
        const output = document.createElement('canvas')
        output.width = right - left
        output.height = bottom - top
        output.getContext('2d').drawImage(
          source,
          left,
          top,
          output.width,
          output.height,
          0,
          0,
          output.width,
          output.height,
        )
        return output.toDataURL('image/png')
      })
      const png = Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64')
      await writeFile(join(outputDirectory, `${filename}-${variant}.png`), png)
      console.log(`Rendered ${filename}-${variant}.png`)
    }
  }
} finally {
  await browser?.close()
  await server.close()
}
