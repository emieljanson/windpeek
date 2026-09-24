<script setup>
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { storeToRefs } from 'pinia'
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { RectAreaLightUniformsLib } from 'three/examples/jsm/lights/RectAreaLightUniformsLib.js'
import { GTAOPass } from 'three/examples/jsm/postprocessing/GTAOPass.js'
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js'
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js'
import { SMAAPass } from 'three/examples/jsm/postprocessing/SMAAPass.js'
import { useConfiguratorStore } from '../stores/configurator'
import {
  createEpaperMaterial,
  createEpaperBacking,
  createMatteScreenFinish,
  createScreenRecessShadow,
  addDeviceRearMarkings,
  enhanceDeviceSurface,
  fitScreenUnderBezel,
} from '../configurator/deviceSurface'
import { hideDeviceStand, loadDeviceModel } from '../configurator/modelLoader'
import { BOARD_IDS } from '../config/configuration'
import {
  applyHeroPose,
  calculateSceneComposition,
  configureOrbitControls,
  createOrbitRendering,
  createHeroEntranceAnimation,
  createUsbCameraAnimation,
  deviceStageForBoard,
  usbCameraForBoard,
  isWebGLAvailable,
} from '../configurator/sceneController'
import { createResourceLifetime, disposeSceneObject, disposeSceneResources } from '../configurator/sceneLifetime'
import { loadSceneResources } from '../configurator/sceneResources'
import { createScreenTexture } from '../configurator/screenTexture'
import { createProductStudioEnvironment } from '../configurator/studioEnvironment'
import { createPerspectiveSurface, createPhysicalShadowLayer, createContactOcclusion } from '../configurator/studioSurface'
import { configureAmbientOcclusion } from '../configurator/ambientOcclusion'
import { createSceneComposer } from '../configurator/sceneComposer'
import { createSceneQuality, SCENE_QUALITY, scenePixelRatio } from '../configurator/sceneQuality'
import { PRODUCT_LIGHTING, DARK_PRODUCT_LIGHTING } from '../configurator/productLighting'
import { scheduleSceneLoadingLabel } from '../configurator/sceneLoadingState'
import SceneDebugLabs from './SceneDebugLabs.vue'
import { markingGroupForSourceMesh } from '../configurator/markingDebug'
import {
  cablePoseAt,
  createUsbCable,
  createUsbCableAnimation,
  USB_CABLE_DISTANCE_FADE_START,
  USB_CABLE_DISTANCE_FADE_END,
} from '../configurator/usbCable'

const props = defineProps({
  boardId: { type: String, default: BOARD_IDS.E1002 },
  captureMode: { type: Boolean, default: false },
  focusUsbConnection: { type: Boolean, default: false },
  showUsbCable: { type: Boolean, default: false },
})
const emit = defineEmits(['ready', 'error'])
const cableLabEnabled = import.meta.env.DEV
  && new URLSearchParams(window.location.search).has('cableLab')
const markingsLabEnabled = import.meta.env.DEV
  && new URLSearchParams(window.location.search).has('debugMarkings')
const markingGroupLabels = Object.freeze({
  TOP_CONTROLS: 'Top buttons',
  MICRO_SD: 'SD card',
  POWER_SWITCH: 'OFF / ON',
  STATUS_CIRCLE: 'Round icon',
  LIGHTNING_BOLT: 'Lightning bolt',
  USB_C: 'USB icon',
  EXPANSION_PORT: 'Right connector',
})
const markingInitialOffsets = Object.freeze({
  TOP_CONTROLS: Object.freeze({ x: 0, y: 0 }),
  MICRO_SD: Object.freeze({ x: 0, y: 0 }),
  POWER_SWITCH: Object.freeze({ x: 0, y: 0 }),
  STATUS_CIRCLE: Object.freeze({ x: 0, y: 0 }),
  LIGHTNING_BOLT: Object.freeze({ x: 0, y: 0 }),
  USB_C: Object.freeze({ x: 0, y: 0 }),
  EXPANSION_PORT: Object.freeze({ x: 0, y: 0 }),
})
const markingOffsets = reactive(Object.fromEntries(
  Object.keys(markingGroupLabels).map((group) => [group, { ...markingInitialOffsets[group] }]),
))
const markingCopyStatus = ref('')
const markingsLabOpen = ref(true)
const markingValuesJson = computed(() => JSON.stringify(Object.fromEntries(
  Object.entries(markingOffsets).map(([group, offset]) => [
    group,
    { xMm: Number(offset.x.toFixed(1)), yMm: Number(offset.y.toFixed(1)) },
  ]),
), null, 2))
const cableLab = reactive({
  distance: 0.9,
  gridHorizonEnd: 3.2,
  gridHorizonStart: 0.9,
  hazeEnd: 1.2,
  hazeStart: 0.5,
  speed: 0.45,
})
const cableLabDuration = computed(() => cableLab.distance / cableLab.speed)
const host = ref(null)
const status = ref('loading')
const showLoadingStatus = ref(false)
const store = useConfiguratorStore()
const {
  forecast,
  forecastRevision,
  effectiveShowTide,
  pendingForecastRevision,
  selectedModelId,
  selectedSpotId,
  showDedicatedFooter,
  showThreshold,
  threshold,
  showWeather,
  showTemperature,
  temperatureUnit,
  timeFormat,
  tide,
  swell,
  swellFocus,
  windSize,
  swellSize,
  swellStatus,
  moduleOrder,
} = storeToRefs(store)

let renderer
let composer
let gtaoPass
let smaaPass
let outputPass
let scene
let camera
let controls
let orbitRendering
let animationFrame
let resizeObserver
let settingsPanel
let viewportResizeFrame
let cancelLoadingStatus = () => {}
let compositionMode
let screenSource

defineExpose({
  exportScreen() {
    if (!screenSource) return null
    return screenSource.exportPng()
  },
})
let keyLight
let softbox
let accent
let rimLight
let oppositePortFill
let hemisphereLight
let themeQuery
let environmentPalette

function applyStudioTheme() {
  if (!scene || !renderer || !keyLight) return
  const dark = themeQuery.matches && !props.captureMode
  const lighting = dark ? DARK_PRODUCT_LIGHTING : PRODUCT_LIGHTING
  renderer.toneMappingExposure = dark ? 1.4 : 1.0
  scene.background = props.captureMode ? null : new THREE.Color(lighting.background)
  scene.fog = dark ? new THREE.FogExp2(lighting.background, 1.5) : null
  if (environmentPalette !== lighting.environment) {
    const previousEnvironment = environmentTarget
    environmentTarget = createProductStudioEnvironment(renderer, lighting.environment)
    environmentPalette = lighting.environment
    scene.environment = environmentTarget.texture
    previousEnvironment?.dispose()
  }
  hemisphereLight.color.set(lighting.hemisphere.sky)
  hemisphereLight.groundColor.set(lighting.hemisphere.ground)
  hemisphereLight.intensity = lighting.hemisphere.intensity
  if (oppositePortFill) {
    oppositePortFill.intensity = dark ? lighting.rim.intensity : 0
    oppositePortFill.color.set(lighting.rim.color)
    oppositePortFill.position.set(-lighting.rim.position[0], lighting.rim.position[1], lighting.rim.position[2])
  }
  for (const [light, settings] of [[keyLight, lighting.key], [softbox, lighting.softbox], [accent, lighting.accent], [rimLight, lighting.rim]]) {
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
  keyLight.angle = lighting.key.angle
  keyLight.penumbra = lighting.key.penumbra
  keyLight.target.position.set(...(lighting.key.target ?? [0, -0.02, 0]))
  if (dark && props.boardId === BOARD_IDS.E1003) {
    // Cover the larger face evenly instead of leaving its edges in shadow.
    keyLight.angle = 0.75
    keyLight.intensity *= 1.4
    softbox.width = 0.32
    softbox.height = 0.28
    softbox.intensity *= 2
    hemisphereLight.intensity *= 2
  }
  const floor = scene.getObjectByName('STUDIO_LIT_FLOOR')
  if (floor) floor.visible = dark
  usbCable?.setDistanceFade(
    dark ? 0.055 : USB_CABLE_DISTANCE_FADE_START,
    dark ? 0.22 : USB_CABLE_DISTANCE_FADE_END,
  )
  const shadow = scene.getObjectByName('PHYSICAL_SHADOW_LAYER')
  if (shadow) shadow.visible = !dark // The dark floor already receives real shadows.
  const contact = scene.getObjectByName('CONTACT_OCCLUSION')
  if (contact) contact.material.uniforms.contactColor.value.setRGB(...(dark ? [0, 0, 0] : [0.075, 0.082, 0.078]))
  const grid = scene.getObjectByName('SURFACE_GRID')
  if (grid) {
    grid.visible = true
    grid.material.uniforms.lineColor.value.set(dark ? 0x7f7f81 : 0x6f7784)
    grid.material.uniforms.lineOpacity.value = dark ? 0.20 / 1.4 : 0.24
    grid.material.uniforms.stageFadeStart.value = dark ? 0.055 : 0.28
    grid.material.uniforms.stageFadeEnd.value = dark ? 0.22 : 0.82
  }
  // These artistic reflection overlays describe the light studio's softboxes.
  // The dark studio uses the physical material and its own reflected lights.
  model?.traverse((child) => {
    // Camera distance must not darken the product. Atmospheric fading belongs
    // to the stage, while the device stays under the same studio lights.
    const materials = Array.isArray(child.material) ? child.material : [child.material]
    for (const material of materials) {
      if (!material?.fog) continue
      material.fog = false
      material.needsUpdate = true
    }
    if (child.name === 'FRONT_PANEL_REFLECTION' || child.name === 'SCREEN_FINISH') child.visible = !dark
  })
  if (cableLabEnabled) applyCableLabSettings()
  requestRender()
}
let usbCable
let usbCableAnimation
let heroEntranceAnimation
let heroEntranceActive = false
let usbCameraAnimation
let reduceMotionQuery
const sceneQuality = createSceneQuality()
const qualityLevel = ref(sceneQuality.level)

function currentDisplayConfig() {
  return {
    showThreshold: showThreshold.value,
    threshold: threshold.value,
    showWeather: showWeather.value,
    showTemperature: showTemperature.value,
    showTide: effectiveShowTide.value,
    showDedicatedFooter: showDedicatedFooter.value,
    timeFormat: timeFormat.value,
    temperatureUnit: temperatureUnit.value,
    tide: tide.value,
    swell: swell.value,
    swellFocus: swellFocus.value,
    windSize: windSize.value,
    swellSize: swellSize.value,
    swellStatus: swellStatus.value,
    moduleOrder: [...moduleOrder.value],
  }
}
let model
let environmentTarget
let disposeSurface
let disposeRearMarkings
let markingBasePositions = new Map()
const lifetime = createResourceLifetime()

function resize() {
  if (!renderer || !camera || !host.value) return
  const width = Math.max(host.value.clientWidth, 1)
  const height = Math.max(host.value.clientHeight, 1)
  const hostBounds = host.value.getBoundingClientRect()
  const settingsBounds = settingsPanel?.getBoundingClientRect()
  const settingsTop = settingsBounds ? settingsBounds.top - hostBounds.top : height
  const panelIsCentered = settingsBounds && Math.abs(
    (settingsBounds.left - hostBounds.left) - (hostBounds.right - settingsBounds.right),
  ) <= 1
  const panelPlacement = settingsPanel?.classList.contains('settings-panel--compact') || panelIsCentered
    ? 'overlay'
    : settingsBounds && settingsBounds.top < hostBounds.bottom
      ? 'side'
      : 'stacked'
  const composition = calculateSceneComposition({ width, height, settingsTop, panelPlacement })
  const deviceStage = deviceStageForBoard(props.boardId)
  const nextCompositionMode = panelPlacement === 'side' ? 'wide' : 'compact'
  usbCable?.setCompositionMode(nextCompositionMode)
  const quality = SCENE_QUALITY[qualityLevel.value]
  const pixelRatio = scenePixelRatio(qualityLevel.value, width, height, window.devicePixelRatio)
  if (renderer.getPixelRatio() !== pixelRatio) {
    renderer.setPixelRatio(pixelRatio)
    composer?.setPixelRatio(pixelRatio)
  }
  renderer.setSize(width, height, false)
  composer?.setSize(width, height)
  gtaoPass?.setSize(Math.round(width * pixelRatio * quality.aoScale), Math.round(height * pixelRatio * quality.aoScale))
  renderer.shadowMap.needsUpdate = true
  camera.aspect = width / height
  camera.zoom = composition.zoom * deviceStage.heroZoom * (props.captureMode ? 1.35 : 1)
  const cableFocused = props.focusUsbConnection
  if (controls && (status.value === 'loading' || (compositionMode !== nextCompositionMode && !cableFocused))) {
    heroEntranceAnimation?.finish()
    heroEntranceActive = false
    applyHeroPose(camera, controls, width / height, nextCompositionMode === 'compact')
  }
  compositionMode = nextCompositionMode
  // Keep the orbit target on the product while composing it inside the space
  // left free by the panel in its current placement.
  if (composition.viewOffsetX || composition.viewOffsetY) {
    camera.setViewOffset(
      width,
      height,
      composition.viewOffsetX,
      composition.viewOffsetY,
      width,
      height,
    )
  }
  else camera.clearViewOffset()
  camera.updateProjectionMatrix()
  requestRender()
}

function scheduleViewportResize() {
  if (viewportResizeFrame !== undefined) return
  viewportResizeFrame = requestAnimationFrame(() => {
    viewportResizeFrame = undefined
    resize()
  })
}

function renderFrame(timestamp) {
  animationFrame = undefined
  const heroEntranceAnimating = heroEntranceActive
    ? (heroEntranceAnimation?.update(timestamp) ?? false)
    : false
  heroEntranceActive = heroEntranceAnimating
  const usbCameraAnimating = usbCameraAnimation?.update(timestamp) ?? false
  const cameraAnimating = heroEntranceAnimating || usbCameraAnimating
  const changed = cameraAnimating ? false : (orbitRendering?.update() ?? false)
  const cableAnimating = usbCableAnimation?.update(timestamp) ?? false
  if (composer) composer.render()
  else renderer?.render(scene, camera)
  const active = changed || cameraAnimating || cableAnimating
  if (sceneQuality.sample(timestamp, active)) {
    qualityLevel.value = sceneQuality.level
    const quality = SCENE_QUALITY[qualityLevel.value]
    gtaoPass.enabled = quality.ao
    gtaoPass.updateGtaoMaterial({ samples: quality.aoSamples })
    resize()
  }
  if (active) requestRender()
}

function requestRender() {
  if (!lifetime.active || document.hidden || animationFrame !== undefined) return
  animationFrame = requestAnimationFrame(renderFrame)
}

function handleSceneVisibility() {
  sceneQuality.pause()
  if (document.hidden && animationFrame !== undefined) {
    cancelAnimationFrame(animationFrame)
    animationFrame = undefined
  }
  requestRender()
}

function startLoadingStatus() {
  cancelLoadingStatus()
  showLoadingStatus.value = false
  cancelLoadingStatus = scheduleSceneLoadingLabel(() => {
    if (lifetime.active && status.value === 'loading') showLoadingStatus.value = true
  })
}

function stopLoadingStatus() {
  cancelLoadingStatus()
  cancelLoadingStatus = () => {}
  showLoadingStatus.value = false
}

function captureMarkingPositions() {
  markingBasePositions = new Map()
  if (!markingsLabEnabled || props.boardId !== BOARD_IDS.E1002) return
  model?.traverse((object) => {
    if (object.userData?.role === 'MARKINGS' && object.userData.markingGroup) {
      const group = markingGroupForSourceMesh(
        object.userData.sourceMesh,
        object.userData.markingGroup,
      )
      if (markingOffsets[group]) {
        markingBasePositions.set(object, { basePosition: object.position.clone(), group })
      }
    }
  })
}

function applyMarkingOffsets() {
  for (const [object, { basePosition, group }] of markingBasePositions) {
    const offset = markingOffsets[group]
    object.position.set(
      basePosition.x + (offset?.x ?? 0) / 1000,
      basePosition.y + (offset?.y ?? 0) / 1000,
      basePosition.z,
    )
  }
  requestRender()
}

function showMarkingsRearView() {
  if (!camera || !controls) return
  heroEntranceAnimation?.finish()
  heroEntranceActive = false
  camera.position.set(0, 0.015, -0.42)
  controls.target.set(0, 0, 0)
  controls.update()
  requestRender()
}

function resetMarkingOffsets() {
  for (const [group, offset] of Object.entries(markingOffsets)) {
    offset.x = markingInitialOffsets[group].x
    offset.y = markingInitialOffsets[group].y
  }
  markingCopyStatus.value = ''
}

async function copyMarkingOffsets() {
  let textarea
  try {
    if (navigator.clipboard?.writeText) await navigator.clipboard.writeText(markingValuesJson.value)
    else {
      textarea = document.createElement('textarea')
      textarea.value = markingValuesJson.value
      textarea.style.position = 'fixed'
      textarea.style.opacity = '0'
      document.body.append(textarea)
      textarea.select()
      if (!document.execCommand('copy')) throw new Error('Copy unavailable')
    }
    markingCopyStatus.value = 'Copied'
  } catch {
    markingCopyStatus.value = 'Select JSON below'
  } finally {
    textarea?.remove()
  }
}

function updateUsbCableVisibility(visible) {
  usbCableAnimation?.setVisible(visible)
}

function updateSceneFocus() {
  if (props.focusUsbConnection) {
    heroEntranceAnimation?.finish()
    heroEntranceActive = false
  }
  usbCameraAnimation?.setUsbView(props.focusUsbConnection)
}

function applyCableLabSettings() {
  if (!cableLabEnabled) return
  const connectedX = cablePoseAt(1, compositionMode, undefined, props.boardId).connector.x
  const hazeEnd = Math.max(cableLab.hazeStart + 0.02, cableLab.hazeEnd)
  usbCable?.setTravelStartX(connectedX + cableLab.distance)
  usbCable?.setDistanceFade(cableLab.hazeStart, hazeEnd)
  usbCableAnimation?.setDuration(cableLabDuration.value * 1000)
  const gridUniforms = scene?.getObjectByName('SURFACE_GRID')?.material?.uniforms
  if (gridUniforms) {
    gridUniforms.stageFadeStart.value = cableLab.hazeStart
    gridUniforms.stageFadeEnd.value = hazeEnd
    gridUniforms.horizonFadeStart.value = cableLab.gridHorizonStart
    gridUniforms.horizonFadeEnd.value = Math.max(
      cableLab.gridHorizonStart + 0.05,
      cableLab.gridHorizonEnd,
    )
  }
  requestRender()
}

function runCableLab(visible) {
  applyCableLabSettings()
  usbCableAnimation?.setVisible(visible)
}

function setCableLabCamera(view) {
  if (view === 'cable') usbCameraAnimation?.setCableView(true)
  else usbCameraAnimation?.setUsbView(view === 'usb')
}

function handleReducedMotionChange(event) {
  requestRender()
  if (!event.matches) return
  usbCableAnimation?.finishForReducedMotion()
  heroEntranceAnimation?.finishForReducedMotion()
  heroEntranceActive = false
  usbCameraAnimation?.finishForReducedMotion()
}

async function initialize() {
  if (!host.value || !isWebGLAvailable()) {
    status.value = 'error'
    emit('error', 'This browser cannot show the 3D model.')
    return
  }

  startLoadingStatus()

  try {
    const lighting = PRODUCT_LIGHTING
    RectAreaLightUniformsLib.init()
    scene = new THREE.Scene()
    const studioBackground = getComputedStyle(document.documentElement)
      .getPropertyValue('--studio-background')
      .trim()
    scene.background = props.captureMode
      ? null
      : new THREE.Color(studioBackground || lighting.background)
    camera = new THREE.PerspectiveCamera(29, 1, 0.01, 10)
    renderer = new THREE.WebGLRenderer({
      antialias: true,
      alpha: true,
      powerPreference: 'high-performance',
      preserveDrawingBuffer: props.captureMode,
    })
    if (props.captureMode) renderer.setClearColor(0x000000, 0)
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    renderer.outputColorSpace = THREE.SRGBColorSpace
    renderer.toneMapping = THREE.NeutralToneMapping
    renderer.toneMappingExposure = 1.0
    renderer.shadowMap.enabled = true
    renderer.shadowMap.type = THREE.PCFShadowMap
    renderer.domElement.setAttribute('aria-hidden', 'true')
    host.value.append(renderer.domElement)

    composer = createSceneComposer(renderer)
    composer.addPass(new RenderPass(scene, camera))
    gtaoPass = configureAmbientOcclusion(new GTAOPass(scene, camera, 1, 1))
    const renderAmbientOcclusion = gtaoPass.render.bind(gtaoPass)
    gtaoPass.render = (...args) => {
      const grid = scene.getObjectByName('SURFACE_GRID')
      if (!grid?.visible) return renderAmbientOcclusion(...args)
      // GTAO replaces every mesh material with a solid normal material. Hide
      // the transparent raster only for its depth pass, then restore it.
      grid.visible = false
      try {
        return renderAmbientOcclusion(...args)
      } finally {
        grid.visible = true
      }
    }
    gtaoPass.updateGtaoMaterial({ samples: SCENE_QUALITY[qualityLevel.value].aoSamples })
    composer.addPass(gtaoPass)
    smaaPass = new SMAAPass()
    composer.addPass(smaaPass)
    outputPass = new OutputPass()
    composer.addPass(outputPass)

    controls = new OrbitControls(camera, renderer.domElement)
    configureOrbitControls(controls)
    orbitRendering = createOrbitRendering(controls, requestRender)
    settingsPanel = host.value.closest('.configurator-layout')?.querySelector('.settings-panel')
    resize()
    usbCameraAnimation = createUsbCameraAnimation({
      camera,
      controls,
      reducedMotion: () => reduceMotionQuery.matches,
      requestRender,
      usbPose: usbCameraForBoard(props.boardId),
    })
    heroEntranceAnimation = createHeroEntranceAnimation({
      camera,
      controls,
      reducedMotion: () => reduceMotionQuery.matches,
      requestRender,
    })
    updateSceneFocus()

    hemisphereLight = new THREE.HemisphereLight(
      lighting.hemisphere.sky,
      lighting.hemisphere.ground,
      lighting.hemisphere.intensity,
    )
    scene.add(hemisphereLight)
    keyLight = new THREE.SpotLight(lighting.key.color, lighting.key.intensity)
    keyLight.position.set(...lighting.key.position)
    keyLight.angle = lighting.key.angle
    keyLight.penumbra = lighting.key.penumbra
    keyLight.decay = lighting.key.decay
    keyLight.distance = lighting.key.distance
    keyLight.castShadow = true
    keyLight.shadow.mapSize.set(1024, 1024)
    keyLight.shadow.camera.near = 0.08
    keyLight.shadow.camera.far = lighting.key.distance
    keyLight.shadow.bias = -0.00002
    keyLight.shadow.normalBias = 0.00012
    keyLight.shadow.radius = 5
    keyLight.target.position.set(0, -0.02, 0)
    scene.add(keyLight, keyLight.target)
    softbox = new THREE.RectAreaLight(
      lighting.softbox.color,
      lighting.softbox.intensity,
      lighting.softbox.width,
      lighting.softbox.height,
    )
    // Mirror the hero camera across the front plane so the satin insert catches
    // one broad, believable highlight instead of only becoming diffusely brighter.
    softbox.position.set(...lighting.softbox.position)
    softbox.lookAt(0, 0, 0)
    scene.add(softbox)
    accent = new THREE.RectAreaLight(
      lighting.accent.color,
      lighting.accent.intensity,
      lighting.accent.width,
      lighting.accent.height,
    )
    accent.position.set(...lighting.accent.position)
    accent.lookAt(0, 0, 0)
    scene.add(accent)
    rimLight = new THREE.DirectionalLight(lighting.rim.color, lighting.rim.intensity)
    rimLight.position.set(...lighting.rim.position)
    scene.add(rimLight)
    oppositePortFill = new THREE.DirectionalLight(lighting.rim.color, 0)
    scene.add(oppositePortFill)
    const deviceStage = deviceStageForBoard(props.boardId)
    if (!props.captureMode) scene.add(createPerspectiveSurface(deviceStage))
    if (!props.captureMode) {
      const floorMaterial = new THREE.MeshStandardMaterial({ color: 0x505052, roughness: 0.92, metalness: 0, envMapIntensity: 0.12, transparent: true, depthWrite: false })
      // Fade the lit floor into the studio background, leaving a soft pool
      // around the product. Local XY is the floor's horizontal plane.
      floorMaterial.onBeforeCompile = (shader) => {
        shader.vertexShader = shader.vertexShader
          .replace('#include <common>', '#include <common>\nvarying vec2 vFloorPosition;')
          .replace('#include <begin_vertex>', '#include <begin_vertex>\nvFloorPosition = position.xy;')
        shader.fragmentShader = shader.fragmentShader
          .replace('#include <common>', '#include <common>\nvarying vec2 vFloorPosition;')
          .replace('#include <color_fragment>', '#include <color_fragment>\ndiffuseColor.a *= 1.0 - smoothstep(0.08, 0.38, length(vFloorPosition));')
          .replace('#include <opaque_fragment>', 'outgoingLight *= 0.8;\n#include <opaque_fragment>')
      }
      const floor = new THREE.Mesh(new THREE.PlaneGeometry(20, 20), floorMaterial)
      floor.name = 'STUDIO_LIT_FLOOR'
      floor.renderOrder = -2
      floor.rotation.x = -Math.PI / 2
      floor.position.y = deviceStage.surfaceY - 0.0001
      floor.receiveShadow = true
      scene.add(floor)
    }
    applyStudioTheme()

    const initialConfig = currentDisplayConfig()
    const initialForecast = forecast.value
    const initialForecastRevision = forecastRevision.value
    const resources = await loadSceneResources({
      lifetime,
      loadModel: () => loadDeviceModel(props.boardId),
      loadScreen: () => createScreenTexture({
        forecast: initialForecast,
        config: initialConfig,
        boardId: props.boardId,
      }),
      disposeModel: disposeObject,
    })
    if (!resources) return
    if (!lifetime.active) {
      disposeSceneObject(resources.model)
      resources.screen.dispose()
      return
    }
    model = resources.model
    screenSource = resources.screen
    hideDeviceStand(model)
    disposeSurface = enhanceDeviceSurface(model, renderer)
    disposeRearMarkings = addDeviceRearMarkings(model, props.boardId, requestRender)
    captureMarkingPositions()
    applyMarkingOffsets()
    model.traverse((child) => {
      if (child.isMesh) {
        child.castShadow = child.name !== 'SCREEN'
        child.receiveShadow = false
      }
    })
    if (showThreshold.value !== initialConfig.showThreshold || threshold.value !== initialConfig.threshold ||
        showWeather.value !== initialConfig.showWeather ||
        showTemperature.value !== initialConfig.showTemperature ||
        showDedicatedFooter.value !== initialConfig.showDedicatedFooter ||
        timeFormat.value !== initialConfig.timeFormat ||
        temperatureUnit.value !== initialConfig.temperatureUnit ||
        effectiveShowTide.value !== initialConfig.showTide || tide.value !== initialConfig.tide ||
        swell.value !== initialConfig.swell || swellStatus.value !== initialConfig.swellStatus || swellFocus.value !== initialConfig.swellFocus ||
        moduleOrder.value.join() !== initialConfig.moduleOrder.join() ||
        windSize.value !== initialConfig.windSize || swellSize.value !== initialConfig.swellSize ||
        forecastRevision.value !== initialForecastRevision) {
      screenSource.update({
        forecast: forecast.value,
        config: currentDisplayConfig(),
      })
    }
    if (pendingForecastRevision.value === forecastRevision.value) store.publishForecast(forecastRevision.value)
    const screen = model.getObjectByName('SCREEN')
    // The CAD display opening is centred 0.85 mm below the imported screen
    // plane. Centre it, then let the full 800×480 surface run underneath the
    // bezel so its rounded inner corners physically clip the display.
    if ([BOARD_IDS.E1001, BOARD_IDS.E1002].includes(props.boardId)) {
      screen.position.y -= 0.00085
      fitScreenUnderBezel(screen)
    }
    screen.material.dispose()
    screen.material = createEpaperMaterial(screenSource.texture)
    const screenBacking = createEpaperBacking(screen)
    createScreenRecessShadow(screenBacking)
    createMatteScreenFinish(screenBacking)
    const initialCompositionMode = host.value.clientWidth <= 56 * 16 ? 'compact' : 'wide'
    compositionMode = initialCompositionMode
    usbCable = createUsbCable(initialCompositionMode, props.boardId)
    usbCableAnimation = createUsbCableAnimation({
      cable: usbCable,
      reducedMotion: () => reduceMotionQuery.matches,
      requestRender,
    })
    if (cableLabEnabled) applyCableLabSettings()
    scene.add(
      createPhysicalShadowLayer(deviceStage, props.captureMode ? 0.07 : 0.28),
      createContactOcclusion(deviceStage, props.captureMode ? 0.25 : 1),
      model,
      usbCable.object,
    )
    applyStudioTheme()
    updateUsbCableVisibility(props.showUsbCable)
    requestRender()

    resizeObserver = new ResizeObserver(resize)
    resizeObserver.observe(host.value)
    if (settingsPanel) resizeObserver.observe(settingsPanel)
    window.visualViewport?.addEventListener('resize', scheduleViewportResize)
    window.visualViewport?.addEventListener('scroll', scheduleViewportResize)
    resize()
    stopLoadingStatus()
    status.value = 'ready'
    if (!props.focusUsbConnection && !usbCameraAnimation.isAnimating()) {
      heroEntranceActive = heroEntranceAnimation.start()
    }
    emit('ready')
    requestRender()
  } catch (error) {
    if (!lifetime.active) return
    stopLoadingStatus()
    status.value = 'error'
    emit('error', error instanceof Error ? error.message : 'The 3D model could not be loaded.')
  }
}

watch([
  showThreshold,
  threshold,
  showWeather,
  showTemperature,
  effectiveShowTide,
  showDedicatedFooter,
  tide,
  swell,
  swellFocus,
  windSize,
  swellSize,
  swellStatus,
  moduleOrder,
  timeFormat,
  temperatureUnit,
], () => {
  try {
    screenSource?.update({ config: currentDisplayConfig() })
  } catch {
    store.reportConfigurationRenderFailure()
  }
  requestRender()
})

watch(forecastRevision, () => {
  if (!screenSource) return
  try {
    screenSource.update({
      forecast: forecast.value,
      config: currentDisplayConfig(),
    })
    store.publishForecast(forecastRevision.value)
    requestRender()
  } catch {
    store.rejectForecastPublication(forecastRevision.value)
    requestRender()
  }
})

watch(() => props.focusUsbConnection, updateSceneFocus)
watch(() => props.showUsbCable, updateUsbCableVisibility)
watch(cableLab, applyCableLabSettings)
watch(markingOffsets, applyMarkingOffsets, { deep: true })

onMounted(() => {
  themeQuery = window.matchMedia('(prefers-color-scheme: dark)')
  themeQuery.addEventListener('change', applyStudioTheme)
  reduceMotionQuery = window.matchMedia('(prefers-reduced-motion: reduce)')
  reduceMotionQuery.addEventListener('change', handleReducedMotionChange)
  document.addEventListener('visibilitychange', handleSceneVisibility)
  initialize()
})
onBeforeUnmount(() => {
  themeQuery?.removeEventListener('change', applyStudioTheme)
  lifetime.cancel()
  stopLoadingStatus()
  if (animationFrame !== undefined) cancelAnimationFrame(animationFrame)
  if (viewportResizeFrame !== undefined) cancelAnimationFrame(viewportResizeFrame)
  resizeObserver?.disconnect()
  window.visualViewport?.removeEventListener('resize', scheduleViewportResize)
  window.visualViewport?.removeEventListener('scroll', scheduleViewportResize)
  reduceMotionQuery?.removeEventListener('change', handleReducedMotionChange)
  document.removeEventListener('visibilitychange', handleSceneVisibility)
  disposeSceneResources({
    orbitRendering, controls, screenSource, disposeSurface, disposeRearMarkings,
    environmentTarget, keyLight, gtaoPass, smaaPass, outputPass, composer,
    model, scene, usbCable, renderer,
  })
})

</script>

<template>
  <div
    ref="host"
    class="scene-host"
    :data-scene-status="status"
    :data-scene-quality="qualityLevel"
    :data-forecast-spot="selectedSpotId"
    :data-forecast-model="selectedModelId"
    :data-forecast-revision="forecastRevision"
  >
    <span v-if="status === 'loading' && showLoadingStatus" class="scene-status" role="status">Building your Windpeek…</span>
    <SceneDebugLabs
      v-if="cableLabEnabled || markingsLabEnabled"
      :board-id="boardId"
      :cable-lab-enabled="cableLabEnabled"
      :markings-lab-enabled="markingsLabEnabled"
      :markings-lab-open="markingsLabOpen"
      :cable-lab="cableLab"
      :cable-lab-duration="cableLabDuration"
      :marking-group-labels="markingGroupLabels"
      :marking-offsets="markingOffsets"
      :marking-copy-status="markingCopyStatus"
      :marking-values-json="markingValuesJson"
      @run-cable="runCableLab"
      @camera="setCableLabCamera"
      @set-markings-open="markingsLabOpen = $event"
      @rear-view="showMarkingsRearView"
      @reset-markings="resetMarkingOffsets"
      @copy-markings="copyMarkingOffsets"
    />
  </div>
</template>

<style scoped>
.scene-host {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  min-height: 0;
  overflow: hidden;
  cursor: grab;
}
.scene-host:active { cursor: grabbing; }
.scene-host :deep(canvas) { display: block; width: 100%; height: 100%; }
.scene-status {
  position: absolute;
  inset: 50% auto auto 50%;
  color: var(--studio-status);
  font: 500 0.7rem/1 'JetBrains Mono Variable', monospace;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  transform: translate(-50%, -50%);
}
</style>
