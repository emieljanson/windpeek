import * as THREE from 'three'
import { RectAreaLightUniformsLib } from 'three/examples/jsm/lights/RectAreaLightUniformsLib.js'
import { BOARD_IDS } from '../config/configuration'
import { PRODUCT_LIGHTING, DARK_PRODUCT_LIGHTING } from './productLighting'
import { createProductStudioEnvironment } from './studioEnvironment'

export function createStudioLighting(scene, renderer, boardId) {
  RectAreaLightUniformsLib.init()
  const lighting = PRODUCT_LIGHTING
  const hemisphere = new THREE.HemisphereLight(
    lighting.hemisphere.sky, lighting.hemisphere.ground, lighting.hemisphere.intensity,
  )
  const key = new THREE.SpotLight(lighting.key.color, lighting.key.intensity)
  key.position.set(...lighting.key.position)
  key.angle = lighting.key.angle
  key.penumbra = lighting.key.penumbra
  key.decay = lighting.key.decay
  key.distance = lighting.key.distance
  key.castShadow = true
  key.shadow.mapSize.set(1024, 1024)
  key.shadow.camera.near = 0.08
  key.shadow.camera.far = lighting.key.distance
  key.shadow.bias = -0.00002
  key.shadow.normalBias = 0.00012
  key.shadow.radius = 5
  key.target.position.set(0, -0.02, 0)
  const softbox = new THREE.RectAreaLight(
    lighting.softbox.color, lighting.softbox.intensity,
    lighting.softbox.width, lighting.softbox.height,
  )
  softbox.position.set(...lighting.softbox.position)
  softbox.lookAt(0, 0, 0)
  const accent = new THREE.RectAreaLight(
    lighting.accent.color, lighting.accent.intensity,
    lighting.accent.width, lighting.accent.height,
  )
  accent.position.set(...lighting.accent.position)
  accent.lookAt(0, 0, 0)
  const rim = new THREE.DirectionalLight(lighting.rim.color, lighting.rim.intensity)
  rim.position.set(...lighting.rim.position)
  const oppositePortFill = new THREE.DirectionalLight(lighting.rim.color, 0)
  scene.add(hemisphere, key, key.target, softbox, accent, rim, oppositePortFill)

  let environmentTarget
  let environmentPalette
  function apply({ dark, captureMode }) {
    const palette = dark ? DARK_PRODUCT_LIGHTING : PRODUCT_LIGHTING
    renderer.toneMappingExposure = dark ? 1.4 : 1.0
    scene.background = captureMode ? null : new THREE.Color(palette.background)
    scene.fog = dark ? new THREE.FogExp2(palette.background, 1.5) : null
    if (environmentPalette !== palette.environment) {
      const previous = environmentTarget
      environmentTarget = createProductStudioEnvironment(renderer, palette.environment)
      environmentPalette = palette.environment
      scene.environment = environmentTarget.texture
      previous?.dispose()
    }
    hemisphere.color.set(palette.hemisphere.sky)
    hemisphere.groundColor.set(palette.hemisphere.ground)
    hemisphere.intensity = palette.hemisphere.intensity
    oppositePortFill.intensity = dark ? palette.rim.intensity : 0
    oppositePortFill.color.set(palette.rim.color)
    oppositePortFill.position.set(-palette.rim.position[0], palette.rim.position[1], palette.rim.position[2])
    for (const [light, settings] of [[key, palette.key], [softbox, palette.softbox], [accent, palette.accent], [rim, palette.rim]]) {
      light.color.set(settings.color)
      light.intensity = settings.intensity
      light.position.set(...settings.position)
      if (light.isRectAreaLight) {
        light.width = settings.width
        light.height = settings.height
      }
    }
    softbox.lookAt(0, 0, 0)
    accent.lookAt(0, 0, 0)
    key.angle = palette.key.angle
    key.penumbra = palette.key.penumbra
    key.target.position.set(...(palette.key.target ?? [0, -0.02, 0]))
    if (dark && boardId === BOARD_IDS.E1003) {
      key.angle = 0.75
      key.intensity *= 1.4
      softbox.width = 0.32
      softbox.height = 0.28
      softbox.intensity *= 2
      hemisphere.intensity *= 2
    }
  }

  return {
    apply,
    get environmentTarget() { return environmentTarget },
    keyLight: key,
  }
}
