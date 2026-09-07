import { describe, expect, it } from 'vitest'
import { createSceneQuality, scenePixelRatio } from '../src/configurator/sceneQuality'

function frames(quality, start, count, interval, active = true) {
  for (let i = 0; i < count; i++) quality.sample(start + i * interval, active)
}

describe('adaptive scene quality', () => {
  it('keeps full detail at 60 fps and ignores idle gaps', () => {
    const quality = createSceneQuality()
    frames(quality, 0, 600, 1000 / 60)
    expect(quality.level).toBe('studio')
    frames(quality, 20000, 10, 1000, false)
    expect(quality.level).toBe('studio')
  })

  it('steps down on sustained slow frames, with a cooldown between changes', () => {
    const quality = createSceneQuality()
    frames(quality, 0, 100, 33.3)
    expect(quality.level).toBe('balanced')
    frames(quality, 3330, 30, 33.3)
    expect(quality.level).toBe('balanced')
    frames(quality, 4329, 300, 33.3)
    expect(quality.level).toBe('efficient')
    frames(quality, 20000, 300, 1000 / 60)
    expect(quality.level).toBe('efficient')
  })

  it('does not downgrade for a single shader compile or a short transition', () => {
    const quality = createSceneQuality()
    frames(quality, 0, 120, 1000 / 60)
    quality.sample(2200, true)
    frames(quality, 2220, 120, 1000 / 60)
    quality.sample(4220, false)
    frames(quality, 6000, 20, 40)
    quality.sample(6800, false)
    frames(quality, 10000, 120, 1000 / 60)
    expect(quality.level).toBe('studio')
  })

  it('does not downgrade further while rendering is paused for a hidden tab', () => {
    const quality = createSceneQuality()
    frames(quality, 0, 24, 300)
    expect(quality.level).toBe('balanced')
    quality.pause()
    frames(quality, 100000, 200, 1000 / 60)
    expect(quality.level).toBe('balanced')
  })

  it('caps pixel work on large and dense displays without increasing low-DPI screens', () => {
    expect(scenePixelRatio('studio', 1280, 720, 2)).toBe(2)
    expect(scenePixelRatio('studio', 3840, 2160, 2)).toBeLessThan(1)
    expect(scenePixelRatio('studio', 390, 844, 3)).toBe(2)
    expect(scenePixelRatio('studio', 1280, 720, 1)).toBe(1)
    expect(scenePixelRatio('efficient', 1280, 720, 2)).toBeLessThan(1.5)
  })
})
