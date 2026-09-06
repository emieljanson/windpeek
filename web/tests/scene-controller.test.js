import { describe, expect, it, vi } from 'vitest'
import { PerspectiveCamera } from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { BOARD_IDS } from '../src/config/configuration'
import {
  HERO_CAMERA,
  HERO_ENTRANCE_DURATION_MS,
  NARROW_CAMERA,
  ORBIT_LIMITS,
  USB_CAMERA,
  E1003_USB_CAMERA,
  USB_CAMERA_DURATION_MS,
  applyHeroPose,
  calculateSceneComposition,
  configureOrbitControls,
  createOrbitRendering,
  createHeroEntranceAnimation,
  createUsbCameraAnimation,
  deviceStageForBoard,
  usbCameraForBoard,
  easeCameraMovement,
  springCameraMovement,
} from '../src/configurator/sceneController'

function vector(values) {
  return {
    x: values[0],
    y: values[1],
    z: values[2],
    set(x, y, z) { this.x = x; this.y = y; this.z = z },
    toArray() { return [this.x, this.y, this.z] },
  }
}

describe('scene controller', () => {
  it('stops rendering at the orbit zoom limit despite floating-point clamp noise', () => {
    const camera = new PerspectiveCamera()
    camera.position.set(0, 0.17440423590791604, 0.04453271266581413)
    const controls = new OrbitControls(camera, document.createElement('canvas'))
    configureOrbitControls(controls)
    const requestRender = vi.fn()
    const orbit = createOrbitRendering(controls, requestRender)
    for (let i = 0; i < 10; i++) expect(orbit.update()).toBe(false)
    expect(requestRender).not.toHaveBeenCalled()
    controls.dispatchEvent({ type: 'change' })
    expect(requestRender).toHaveBeenCalledOnce()
    orbit.dispose()
    controls.dispatchEvent({ type: 'change' })
    expect(requestRender).toHaveBeenCalledOnce()
    controls.dispose()
  })
  it('frames the USB socket for the selected physical device', () => {
    expect(usbCameraForBoard(BOARD_IDS.E1002)).toBe(USB_CAMERA)
    expect(usbCameraForBoard(BOARD_IDS.E1003)).toBe(E1003_USB_CAMERA)
    expect(E1003_USB_CAMERA.target).not.toEqual(USB_CAMERA.target)
  })

  it('grounds the taller E1003 on its own product stage', () => {
    const e1002 = deviceStageForBoard(BOARD_IDS.E1002)
    const e1003 = deviceStageForBoard(BOARD_IDS.E1003)

    expect(e1003.surfaceY).toBeLessThan(e1002.surfaceY)
    expect(e1003.contactWidth).toBeGreaterThan(e1002.contactWidth)
    expect(e1003.contactDepth).toBeGreaterThan(e1002.contactDepth)
    expect(e1003.contactWidth / e1002.contactWidth).toBeCloseTo(224 / 176, 2)
    expect(e1003.contactDepth / e1002.contactDepth).toBeCloseTo(18.6 / 17, 2)
    expect(e1003.contactZ / e1002.contactZ).toBeCloseTo(18.6 / 17, 1)
    expect(e1003.contactOpacity).toBeLessThan(e1002.contactOpacity)
    expect(e1003.contactPower).toBeLessThan(e1002.contactPower)
    expect(e1003.heroZoom).toBeLessThan(e1002.heroZoom)
  })
  it('restores the designed camera and orbit target', () => {
    const camera = { position: { set: (...values) => { camera.values = values } } }
    const controls = { target: { set: (...values) => { controls.values = values } }, update: () => { controls.updated = true } }
    applyHeroPose(camera, controls)
    expect(camera.values).toEqual(HERO_CAMERA.position)
    expect(camera.values[0]).toBeLessThan(0)
    expect(Math.abs(camera.values[0])).toBeLessThan(0.2)
    expect(camera.values[1]).toBeLessThan(0.1)
    expect(controls.values).toEqual(HERO_CAMERA.target)
    expect(controls.updated).toBe(true)
  })

  it('fits the same 3D product into a narrow stage', () => {
    const camera = { position: { set: (...values) => { camera.values = values } } }
    const controls = { target: { set: vi.fn() }, update: vi.fn() }
    applyHeroPose(camera, controls, 0.7)
    expect(camera.values).toEqual(NARROW_CAMERA.position)
    expect(camera.values[0]).toBeLessThan(0)
    expect(camera.values[1]).toBeLessThan(0.05)
  })

  it('uses the distant product camera whenever the panel overlays a narrow stage', () => {
    const camera = { position: { set: (...values) => { camera.values = values } } }
    const controls = { target: { set: vi.fn() }, update: vi.fn() }

    applyHeroPose(camera, controls, 0.92, true)

    expect(camera.values).toEqual(NARROW_CAMERA.position)
  })

  it('centres the product in the free space above the floating settings panel', () => {
    const composition = calculateSceneComposition({
      width: 390,
      height: 844,
      settingsTop: 412,
    })

    expect(composition.availableHeight).toBe(400)
    expect(composition.viewOffsetX).toBe(0)
    expect(composition.viewOffsetY).toBe(222)
    expect(844 / 2 - composition.viewOffsetY).toBe(200)
    expect(composition.zoom).toBeCloseTo(0.72, 2)
  })

  it('zooms out only when the remaining mobile preview becomes too short', () => {
    const composition = calculateSceneComposition({
      width: 390,
      height: 667,
      settingsTop: 250,
    })

    expect(composition.availableHeight).toBe(238)
    expect(composition.zoom).toBeLessThanOrEqual(0.73)
    expect(composition.zoom).toBeGreaterThanOrEqual(0.55)
  })

  it('keeps the desktop composition offset toward the free space beside the panel', () => {
    expect(calculateSceneComposition({ width: 1280, height: 800, settingsTop: 16 }))
      .toEqual({
        availableHeight: 800,
        viewOffsetX: 166,
        viewOffsetY: 0,
        zoom: 1,
      })
  })

  it('keeps a narrow desktop model visible when the panel sits beside the stage', () => {
    expect(calculateSceneComposition({
      width: 844,
      height: 390,
      settingsTop: 16,
      panelPlacement: 'side',
    })).toEqual({
      availableHeight: 390,
      viewOffsetX: 110,
      viewOffsetY: 0,
      zoom: 1,
    })
  })

  it('uses the available width instead of unnecessarily shrinking a compact tablet', () => {
    const composition = calculateSceneComposition({
      width: 768,
      height: 900,
      settingsTop: 462,
    })

    expect(composition.zoom).toBe(1)
    expect(composition.viewOffsetY).toBeGreaterThan(0)
  })

  it('allows a complete orbit and a higher top view without moving underneath', () => {
    expect(ORBIT_LIMITS.minAzimuth).toBe(-Infinity)
    expect(ORBIT_LIMITS.maxAzimuth).toBe(Infinity)
    expect(ORBIT_LIMITS.minPolar).toBeLessThan((Math.PI * 20) / 180)
    expect(ORBIT_LIMITS.minPolar).toBeGreaterThan(0)
    expect(ORBIT_LIMITS.maxPolar).toBeLessThan(Math.PI / 2)
    expect(ORBIT_LIMITS.minDistance).toBeGreaterThan(0)
    expect(ORBIT_LIMITS.maxDistance).toBeLessThan(1)
  })

  it('keeps the restrained orbit even when legacy inspection options are passed', () => {
    const controls = {}

    configureOrbitControls(controls, { allowBackView: true })

    expect(controls.minAzimuthAngle).toBe(ORBIT_LIMITS.minAzimuth)
    expect(controls.maxAzimuthAngle).toBe(ORBIT_LIMITS.maxAzimuth)
    expect(controls.maxPolarAngle).toBe(ORBIT_LIMITS.maxPolar)
  })

  it('moves to the USB connection view and retargets smoothly back when interrupted', () => {
    const camera = { position: vector(HERO_CAMERA.position), lookAt: vi.fn() }
    const controls = {
      enabled: true,
      target: vector(HERO_CAMERA.target),
      update: vi.fn(),
    }
    const requestRender = vi.fn()
    const animation = createUsbCameraAnimation({
      camera,
      controls,
      reducedMotion: () => false,
      requestRender,
    })

    expect(animation.isAnimating()).toBe(false)
    animation.setUsbView(true)
    expect(animation.isAnimating()).toBe(true)
    animation.update(100)
    animation.update(500)
    const interruptedPosition = camera.position.toArray()

    expect(interruptedPosition).not.toEqual(HERO_CAMERA.position)
    expect(interruptedPosition).not.toEqual(USB_CAMERA.position)
    expect(Math.hypot(...interruptedPosition)).toBeGreaterThan(0.3)
    expect(camera.lookAt).toHaveBeenLastCalledWith(
      controls.target.x,
      controls.target.y,
      controls.target.z,
    )
    expect(controls.enabled).toBe(false)

    animation.setUsbView(false)
    expect(camera.position.toArray()).toEqual(interruptedPosition)
    animation.update(500)
    animation.update(2_000)

    expect(animation.isAnimating()).toBe(false)
    expect(camera.position.toArray()).toEqual(HERO_CAMERA.position)
    expect(controls.target.toArray()).toEqual(HERO_CAMERA.target)
    expect(controls.enabled).toBe(true)
    expect(requestRender).toHaveBeenCalled()
  })

  it('uses a strong ease-out so the camera spends its final phase settling', () => {
    expect(USB_CAMERA_DURATION_MS).toBe(1500)
    expect(easeCameraMovement(0)).toBe(0)
    expect(easeCameraMovement(0.25)).toBeGreaterThan(0.5)
    expect(easeCameraMovement(0.5)).toBeGreaterThan(0.8)
    expect(easeCameraMovement(1)).toBe(1)
  })

  it('uses a critically damped spring that settles without bounce', () => {
    expect(springCameraMovement(0)).toBe(0)

    const samples = [0, 250, 600, 1_000, 1_400, 1_800].map(springCameraMovement)
    expect(samples.every((value, index) => index === 0 || value > samples[index - 1])).toBe(true)
    expect(samples.every((value) => value >= 0 && value < 1)).toBe(true)
    expect(samples.at(-1)).toBeGreaterThan(0.998)
  })

  it('enters with a small orbit before settling into the hero camera', () => {
    const camera = {
      position: vector(HERO_CAMERA.position),
      lookAt: vi.fn(),
    }
    const controls = {
      enabled: true,
      target: vector(HERO_CAMERA.target),
      update: vi.fn(),
    }
    const requestRender = vi.fn()
    const animation = createHeroEntranceAnimation({
      camera,
      controls,
      reducedMotion: () => false,
      requestRender,
    })

    expect(animation.start()).toBe(true)
    const entrancePosition = camera.position.toArray()

    expect(HERO_ENTRANCE_DURATION_MS).toBe(1800)
    expect(entrancePosition).not.toEqual(HERO_CAMERA.position)
    expect(Math.hypot(...entrancePosition)).toBeGreaterThan(Math.hypot(...HERO_CAMERA.position))
    expect(entrancePosition[1]).toBeGreaterThan(HERO_CAMERA.position[1])
    expect(controls.enabled).toBe(false)

    animation.update(100)
    animation.update(1_900)

    expect(camera.position.toArray()).toEqual(HERO_CAMERA.position)
    expect(controls.target.toArray()).toEqual(HERO_CAMERA.target)
    expect(controls.enabled).toBe(true)
    expect(requestRender).toHaveBeenCalled()

    camera.position.set(0.2, 0.1, 0.3)
    animation.finish()
    expect(camera.position.toArray()).toEqual([0.2, 0.1, 0.3])
  })

  it('settles safely when the entrance is interrupted', () => {
    const camera = { position: vector(HERO_CAMERA.position), lookAt: vi.fn() }
    const controls = { enabled: true, target: vector(HERO_CAMERA.target), update: vi.fn() }
    const requestRender = vi.fn()
    const animation = createHeroEntranceAnimation({
      camera,
      controls,
      reducedMotion: () => false,
      requestRender,
    })

    animation.start()
    animation.update(100)
    animation.update(500)
    animation.finish()

    expect(camera.position.toArray()).toEqual(HERO_CAMERA.position)
    expect(controls.target.toArray()).toEqual(HERO_CAMERA.target)
    expect(controls.enabled).toBe(true)
    expect(animation.update(600)).toBe(false)
  })

  it('settles and redraws when reduced motion is enabled mid-entrance', () => {
    const camera = { position: vector(HERO_CAMERA.position), lookAt: vi.fn() }
    const controls = { enabled: true, target: vector(HERO_CAMERA.target), update: vi.fn() }
    const requestRender = vi.fn()
    const animation = createHeroEntranceAnimation({
      camera,
      controls,
      reducedMotion: () => false,
      requestRender,
    })

    animation.start()
    animation.update(100)
    animation.update(500)
    requestRender.mockClear()
    animation.finishForReducedMotion()

    expect(camera.position.toArray()).toEqual(HERO_CAMERA.position)
    expect(controls.target.toArray()).toEqual(HERO_CAMERA.target)
    expect(controls.enabled).toBe(true)
    expect(requestRender).toHaveBeenCalledOnce()
    expect(animation.update(600)).toBe(false)
  })

  it('keeps the hero camera still when reduced motion is requested', () => {
    const camera = { position: vector(HERO_CAMERA.position), lookAt: vi.fn() }
    const controls = { enabled: true, target: vector(HERO_CAMERA.target), update: vi.fn() }
    const animation = createHeroEntranceAnimation({
      camera,
      controls,
      reducedMotion: () => true,
      requestRender: vi.fn(),
    })

    expect(animation.start()).toBe(false)

    expect(camera.position.toArray()).toEqual(HERO_CAMERA.position)
    expect(controls.enabled).toBe(true)
    expect(animation.update(100)).toBe(false)
  })

  it('uses an immediate position change when reduced motion is requested', () => {
    const camera = { position: vector(HERO_CAMERA.position) }
    const controls = { enabled: true, target: vector(HERO_CAMERA.target), update: vi.fn() }
    const animation = createUsbCameraAnimation({
      camera,
      controls,
      reducedMotion: () => true,
      requestRender: vi.fn(),
    })

    animation.setUsbView(true)

    expect(camera.position.toArray()).toEqual(USB_CAMERA.position)
    expect(controls.target.toArray()).toEqual(USB_CAMERA.target)
    expect(controls.enabled).toBe(true)
  })

})
