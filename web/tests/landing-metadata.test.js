import { existsSync, readFileSync } from 'node:fs'
import { describe, expect, it, vi } from 'vitest'
import { isConfiguratorLocation } from '../src/routes'

describe('landing share metadata', () => {
  it('points crawlers to the production page and a published static preview', () => {
    const html = readFileSync('index.html', 'utf8')
    const head = new DOMParser().parseFromString(html, 'text/html').head
    const imageUrl = new URL(head.querySelector('[property="og:image"]').content)

    expect(head.querySelector('link[rel="canonical"]').href).toBe('https://emieljanson.com/windscout/')
    expect(head.querySelector('[property="og:url"]').content).toBe('https://emieljanson.com/windscout/')
    expect(imageUrl.href).toBe('https://emieljanson.com/windscout/marketing/windscout-social-v17.jpg')
    expect(existsSync(`public/${imageUrl.pathname.replace('/windscout/', '')}`)).toBe(true)
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
      page: '#101210',
      studio: '#f3f5f7',
      landing: '#101210',
      theme: '#101210',
    })
    expect(applySurface('?configure', true)).toEqual({
      page: '#f3f5f7',
      studio: '#f3f5f7',
      landing: '#101210',
      theme: '#f3f5f7',
    })

    let applySchemeChange
    expect(applySurface('', false, (listener) => { applySchemeChange = listener })).toEqual({
      page: '#ffffff',
      studio: '#f3f5f7',
      landing: '#ffffff',
      theme: '#ffffff',
    })
    applySchemeChange({ matches: true })
    expect(currentSurface()).toEqual({
      page: '#101210',
      studio: '#f3f5f7',
      landing: '#101210',
      theme: '#101210',
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
      page: '#f3f5f7',
      studio: '#f3f5f7',
      landing: '#101210',
      theme: '#f3f5f7',
    })
  })
})
