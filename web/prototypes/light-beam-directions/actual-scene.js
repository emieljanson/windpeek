import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js'
import { GTAOPass } from 'three/examples/jsm/postprocessing/GTAOPass.js'
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js'
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js'
import { SMAAPass } from 'three/examples/jsm/postprocessing/SMAAPass.js'
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js'
import {
  createEpaperBacking,
  createEpaperMaterial,
  createMatteScreenFinish,
  createScreenRecessShadow,
  enhanceE1002Surface,
  fitScreenUnderBezel,
} from '../../src/configurator/deviceSurface.js'
import { configureAmbientOcclusion } from '../../src/configurator/ambientOcclusion.js'
import { hideDeviceStand, loadE1002Model } from '../../src/configurator/modelLoader.js'
import { configureOrbitControls, FIRMWARE_CAMERA } from '../../src/configurator/sceneController.js'
import { createProductStudioScene } from '../../src/configurator/studioEnvironment.js'
import { PRODUCT_LIGHTING } from '../../src/configurator/productLighting.js'
import { createScreenTexture } from '../../src/configurator/screenTexture.js'
import { cableCurveForPoints, cablePoseAt, createUsbCable } from '../../src/configurator/usbCable.js'
import { createCableEffects } from './cable-effects.js'

const VARIANT_CONFIG = [
  { opacity: 0.035, preserveConnector: true },
  { opacity: 0.04, preserveConnector: true },
  { opacity: 0.72, preserveConnector: true },
]

class VisibleCableCurve extends THREE.Curve {
  constructor(curve, start = 0.7, end = 0.99) {
    super()
    this.curve = curve
    this.start = start
    this.end = end
  }

  getPoint(t, target = new THREE.Vector3()) {
    return this.curve.getPointAt(THREE.MathUtils.lerp(this.start, this.end, t), target)
  }
}

function addStudioLights(scene, renderer) {
  const lighting = PRODUCT_LIGHTING
  const studio = createProductStudioScene(lighting.environment)
  const generator = new THREE.PMREMGenerator(renderer)
  scene.environment = generator.fromScene(studio, 0.04).texture
  studio.traverse((object) => {
    object.geometry?.dispose?.()
    object.material?.dispose?.()
  })
  generator.dispose()
  scene.add(new THREE.HemisphereLight(lighting.hemisphere.sky, lighting.hemisphere.ground, lighting.hemisphere.intensity))
  const key = new THREE.SpotLight(lighting.key.color, lighting.key.intensity)
  key.position.set(...lighting.key.position)
  key.angle = lighting.key.angle
  key.penumbra = lighting.key.penumbra
  key.decay = lighting.key.decay
  key.distance = lighting.key.distance
  key.castShadow = true
  key.shadow.mapSize.set(1024, 1024)
  key.shadow.bias = -0.00002
  key.shadow.normalBias = 0.00012
  key.shadow.radius = 5
  key.target.position.set(0, -0.02, 0)
  scene.add(key, key.target)
  const softbox = new THREE.RectAreaLight(lighting.softbox.color, lighting.softbox.intensity, lighting.softbox.width, lighting.softbox.height)
  softbox.position.set(...lighting.softbox.position)
  softbox.lookAt(0, 0, 0)
  scene.add(softbox)
  const accent = new THREE.RectAreaLight(lighting.accent.color, lighting.accent.intensity, lighting.accent.width, lighting.accent.height)
  accent.position.set(...lighting.accent.position)
  accent.lookAt(0, 0, 0)
  scene.add(accent)
  const rim = new THREE.DirectionalLight(lighting.rim.color, lighting.rim.intensity)
  rim.position.set(...lighting.rim.position)
  scene.add(rim)
}

function addFloor(scene) {
  const floor = new THREE.Mesh(
    new THREE.PlaneGeometry(3.6, 3.6),
    new THREE.ShadowMaterial({ color: 0x4d524f, opacity: 0.27, transparent: true, depthWrite: false }),
  )
  floor.rotation.x = -Math.PI / 2
  floor.position.y = -0.0604
  floor.receiveShadow = true
  scene.add(floor)
  const gridMaterial = new THREE.ShaderMaterial({
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    extensions: { derivatives: true },
    uniforms: {
      lineColor: { value: new THREE.Color(0x77808a) },
      spacing: { value: 0.032 },
    },
    vertexShader: `
      varying vec3 vWorldPosition;
      void main() {
        vec4 worldPosition = modelMatrix * vec4(position, 1.0);
        vWorldPosition = worldPosition.xyz;
        gl_Position = projectionMatrix * viewMatrix * worldPosition;
      }
    `,
    fragmentShader: `
      uniform vec3 lineColor;
      uniform float spacing;
      varying vec3 vWorldPosition;
      void main() {
        vec2 coordinate = vWorldPosition.xz / spacing;
        vec2 footprint = max(fwidth(coordinate), vec2(0.0001));
        vec2 distanceToLine = abs(fract(coordinate - 0.5) - 0.5) / footprint;
        float line = 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
        float density = smoothstep(3.5, 9.0, 1.0 / max(footprint.x, footprint.y));
        float distanceFade = 1.0 - smoothstep(0.3, 0.86, length(vWorldPosition.xz));
        gl_FragColor = vec4(lineColor, (0.022 + line * density * 0.16) * distanceFade * distanceFade);
      }
    `,
  })
  const grid = new THREE.Mesh(new THREE.PlaneGeometry(8, 8), gridMaterial)
  grid.rotation.x = -Math.PI / 2
  grid.position.y = -0.06025
  grid.renderOrder = -1
  scene.add(grid)
}

function preserveConnector(cable) {
  cable.object.getObjectByName('USB_C_CONNECTOR')?.traverse((object) => {
    if (!object.material) return
    object.material.opacity = 1
    object.material.transparent = false
    object.material.needsUpdate = true
  })
}

export async function createActualScene({ canvas, onReady }) {
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)')
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, powerPreference: 'high-performance' })
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
  renderer.outputColorSpace = THREE.SRGBColorSpace
  renderer.toneMapping = THREE.NeutralToneMapping
  renderer.toneMappingExposure = 1
  renderer.shadowMap.enabled = true
  renderer.shadowMap.type = THREE.PCFShadowMap

  const scene = new THREE.Scene()
  scene.background = new THREE.Color(PRODUCT_LIGHTING.background)
  const camera = new THREE.PerspectiveCamera(29, 1, 0.01, 10)
  camera.position.set(...FIRMWARE_CAMERA.position)
  const controls = new OrbitControls(camera, canvas)
  configureOrbitControls(controls)
  controls.target.set(...FIRMWARE_CAMERA.target)
  controls.update()

  const composer = new EffectComposer(renderer)
  composer.addPass(new RenderPass(scene, camera))
  composer.addPass(configureAmbientOcclusion(new GTAOPass(scene, camera, 1, 1)))
  composer.addPass(new SMAAPass())
  const bloom = new UnrealBloomPass(new THREE.Vector2(1, 1), 0.24, 0.38, 1.08)
  composer.addPass(bloom)
  composer.addPass(new OutputPass())

  addStudioLights(scene, renderer)
  addFloor(scene)

  const model = await loadE1002Model()
  hideDeviceStand(model)
  enhanceE1002Surface(model, renderer)
  model.traverse((object) => {
    if (!object.isMesh) return
    object.castShadow = object.name !== 'SCREEN'
  })
  const screenSource = await createScreenTexture({
    config: {
      showThreshold: true,
      threshold: 18,
      showWeather: true,
      showTemperature: false,
      showTide: false,
      timeFormat: '24-hour',
      temperatureUnit: 'celsius',
    },
  })
  const screen = model.getObjectByName('SCREEN')
  if (screen) {
    screen.position.y -= 0.00085
    fitScreenUnderBezel(screen)
    screen.material.dispose()
    screen.material = createEpaperMaterial(screenSource.texture)
    const backing = createEpaperBacking(screen)
    createScreenRecessShadow(backing)
    createMatteScreenFinish(backing)
  }

  const cable = createUsbCable('wide')
  cable.setProgress(1, true)
  cable.object.visible = true
  cable.setDataActive(false)
  const cableCurve = cableCurveForPoints(cablePoseAt(1, 'wide').cablePoints)
  const effects = createCableEffects(new VisibleCableCurve(cableCurve))
  scene.add(model, cable.object, effects.root)

  let startTime = performance.now()
  let frame

  function applyVariant(index) {
    const config = VARIANT_CONFIG[index]
    cable.setOpacity(1)
    cable.setOpacity(config.opacity)
    if (config.preserveConnector) preserveConnector(cable)
    effects.setVariant(index)
    bloom.strength = [0.2, 0.18, 0.2][index]
    startTime = performance.now()
  }

  function resize() {
    const width = Math.max(canvas.clientWidth, 1)
    const height = Math.max(canvas.clientHeight, 1)
    renderer.setSize(width, height, false)
    composer.setSize(width, height)
    camera.aspect = width / height
    // Keep the full device-to-cable story visible in narrow review panels.
    // A zoom change preserves the exact same viewpoint for all four concepts.
    camera.zoom = THREE.MathUtils.clamp(camera.aspect / 1.24, 0.66, 1)
    camera.updateProjectionMatrix()
  }

  function render(now) {
    const elapsed = (now - startTime) / 1000
    controls.update()
    effects.update(elapsed, reducedMotion.matches)
    cable.updateData(now, reducedMotion.matches)
    composer.render()
    frame = requestAnimationFrame(render)
  }

  window.addEventListener('resize', resize)
  resize()
  applyVariant(0)
  frame = requestAnimationFrame(render)
  onReady?.()

  return {
    setVariant: applyVariant,
    replay() { startTime = performance.now() },
    dispose() {
      cancelAnimationFrame(frame)
      window.removeEventListener('resize', resize)
      controls.dispose()
      effects.dispose()
      cable.dispose()
      composer.dispose()
      screenSource.dispose()
      renderer.dispose()
    },
  }
}
