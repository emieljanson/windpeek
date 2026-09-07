import { existsSync, readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'

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
})
