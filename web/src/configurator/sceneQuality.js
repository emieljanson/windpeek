// One scene, progressively cheaper sampling. Materials, geometry and the grid
// stay identical. Never oscillate quality during an installation.
export const SCENE_QUALITY = Object.freeze({
  studio: Object.freeze({ dpr: 2, pixels: 4_000_000, ao: true, aoScale: 0.75, aoSamples: 16 }),
  balanced: Object.freeze({ dpr: 1.5, pixels: 2_000_000, ao: true, aoScale: 0.5, aoSamples: 8 }),
  efficient: Object.freeze({ dpr: 1.25, pixels: 1_000_000, ao: false, aoScale: 0.5, aoSamples: 8 }),
})
const levels = Object.keys(SCENE_QUALITY)

export function scenePixelRatio(level, width, height, devicePixelRatio) {
  const profile = SCENE_QUALITY[level]
  return Math.min(devicePixelRatio || 1, profile.dpr, Math.sqrt(profile.pixels / Math.max(1, width * height)))
}

export function createSceneQuality() {
  let index = 0
  let lastTime
  let warmUntil = 0
  let elapsed = 0
  let count = 0
  let slow = 0

  function resetWindow() { elapsed = 0; count = 0; slow = 0 }

  return {
    get level() { return levels[index] },
    pause() { lastTime = undefined; resetWindow() },
    sample(timestamp, active) {
      const delta = Math.min(timestamp - lastTime, 1000)
      lastTime = active ? timestamp : undefined
      // Ignore initial shader compilation and on-demand frames. Visibility
      // changes explicitly pause sampling, so genuinely slow frames still count.
      if (!active || !Number.isFinite(delta) || delta <= 0) {
        resetWindow()
        warmUntil = Math.max(warmUntil, timestamp + 800)
        return false
      }
      if (timestamp < warmUntil || index === levels.length - 1) return false
      elapsed += delta
      count++
      if (delta > 24) slow++
      if (elapsed < 1600 || count < 12) return false
      const struggling = elapsed / count > 19 && slow / count > 0.12
      resetWindow()
      if (!struggling) return false
      index++
      warmUntil = timestamp + 4000
      return true
    },
  }
}
