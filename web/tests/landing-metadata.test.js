import { existsSync, readFileSync } from 'node:fs'
import { describe, expect, it, vi } from 'vitest'
import { isConfiguratorLocation } from '../src/routes'

describe('landing share metadata', () => {
  it('points crawlers to the production page and a published static preview', () => {
    const html = readFileSync('index.html', 'utf8')
    const head = new DOMParser().parseFromString(html, 'text/html').head
    const imageUrl = new URL(head.querySelector('[property="og:image"]').content)

    expect(head.querySelector('link[rel="canonical"]').href).toBe('https://windpeek.com/')
    expect(head.querySelector('[property="og:url"]').content).toBe('https://windpeek.com/')
    expect(imageUrl.href).toBe('https://windpeek.com/marketing/windpeek-social-v17.jpg')
    expect(existsSync(`public/${imageUrl.pathname.replace('/windpeek/', '')}`)).toBe(true)
    expect(head.querySelector('[property="og:image:width"]').content).toBe('1200')
    expect(head.querySelector('[property="og:image:height"]').content).toBe('675')
    expect(head.querySelector('[property="og:description"]').content).not.toContain('Preview your own spot')
  })

  it('sets the correct page surface before the app mounts', () => {
    const html = readFileSync('index.html', 'utf8')
    const head = new DOMParser().parseFromString(html, 'text/html').head
    const script = head.querySelector('script[data-page-surface]')?.textContent
    const currentSurface = () => ({
      page: document.documentElement.style.getPropertyValue('--page-background'),
      studio: document.documentElement.style.getPropertyValue('--studio-background'),
      landing: document.documentElement.style.getPropertyValue('--landing-page-background'),
      theme: document.querySelector('meta[name="theme-color"]').content,
    })
    const applySurface = (search, dark = false, captureChangeListener) => {
      document.head.innerHTML = '<meta name="theme-color" content="#ffffff">'
      document.documentElement.style.cssText = ''
      const matchMedia = () => ({
        matches: dark,
        addEventListener: vi.fn((event, listener) => {
          if (event === 'change') captureChangeListener?.(listener)
        }),
      })
      new Function('document', 'location', 'URLSearchParams', 'matchMedia', script)(
        document,
        { search },
        URLSearchParams,
        matchMedia,
      )
      return currentSurface()
    }

    expect(script).toBeTruthy()

    const searches = [
      '',
      '?configure',
      '?configure=1',
      '?devicePreview=seeedstudio_reterminal_e1002',
      '?installerDemo=1',
      '?unrelated=1',
    ]

    for (const search of searches) {
      const surface = applySurface(search)
      const isStudioSurface = surface.page === surface.studio

      expect(isStudioSurface, `surface and app route disagree for ${search || 'landing'}`).toBe(
        isConfiguratorLocation({ search }),
      )
      expect(surface.theme).toBe(surface.page)
    }

    expect(applySurface('')).toEqual({
      page: '#ffffff',
      studio: '#f3f5f7',
      landing: '#ffffff',
      theme: '#ffffff',
    })
    expect(applySurface('?configure')).toEqual({
      page: '#f3f5f7',
      studio: '#f3f5f7',
      landing: '#ffffff',
      theme: '#f3f5f7',
    })
    expect(applySurface('', true)).toEqual({
      page: '#111113',
      studio: '#101012',
      landing: '#111113',
      theme: '#111113',
    })
    expect(applySurface('?configure', true)).toEqual({
      page: '#101012',
      studio: '#101012',
      landing: '#111113',
      theme: '#101012',
    })
    for (const preview of ['', 'unsupported']) {
      expect(applySurface(`?devicePreview=${preview}`, true).studio).toBe('#101012')
    }
    for (const board of ['e1001', 'e1002', 'e1003']) {
      expect(applySurface(`?devicePreview=seeedstudio_reterminal_${board}`, true).studio).toBe('#f3f5f7')
    }

    let applySchemeChange
    expect(applySurface('', false, (listener) => { applySchemeChange = listener })).toEqual({
      page: '#ffffff',
      studio: '#f3f5f7',
      landing: '#ffffff',
      theme: '#ffffff',
    })
    applySchemeChange({ matches: true })
    expect(currentSurface()).toEqual({
      page: '#111113',
      studio: '#101012',
      landing: '#111113',
      theme: '#111113',
    })

    let applyStudioSchemeChange
    applySurface('?configure', true, (listener) => { applyStudioSchemeChange = listener })
    applyStudioSchemeChange({ matches: false })
    expect(currentSurface()).toEqual({
      page: '#f3f5f7',
      studio: '#f3f5f7',
      landing: '#ffffff',
      theme: '#f3f5f7',
    })
    applyStudioSchemeChange({ matches: true })
    expect(currentSurface()).toEqual({
      page: '#101012',
      studio: '#101012',
      landing: '#111113',
      theme: '#101012',
    })
  })
})

it('ships swell metadata in HTML without requiring JavaScript', async () => {
  const { swellPageHtml } = await import('../scripts/swell-page.mjs')
  const { siteVariant } = await import('../src/marketing/siteVariant.js')
  const html = swellPageHtml(readFileSync('index.html', 'utf8'))
  const head = new DOMParser().parseFromString(html, 'text/html').head
  expect(head.querySelector('[property="og:title"]').content).toContain('swell forecast')
  expect(head.querySelector('[property="og:description"]').content).toContain('swell height')
  expect(head.querySelector('link[rel="canonical"]').getAttribute('href')).toBe('https://windpeek.com/swell/')
  expect(head.querySelector('[property="og:url"]').content).toBe('https://windpeek.com/swell/')
  expect(head.querySelector('[property="og:image"]').content).toBe('https://windpeek.com/marketing/windpeek-social-swell-v1.jpg')
  expect(existsSync('public/marketing/windpeek-social-swell-v1.jpg')).toBe(true)
  expect(head.querySelector('base').getAttribute('href')).toBe('../')
  expect(siteVariant({pathname:'/swell/'}).id).toBe('swell')
  expect(siteVariant({pathname:'/swell/',search:'?site=wind'}).id).toBe('wind')
})
