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
import { createResourceLifetime } from '../configurator/sceneLifetime'
import { createScreenTexture } from '../configurator/screenTexture'
import { createProductStudioEnvironment } from '../configurator/studioEnvironment'
import { configureAmbientOcclusion } from '../configurator/ambientOcclusion'
import { createSceneComposer } from '../configurator/sceneComposer'
import { createSceneQuality, SCENE_QUALITY, scenePixelRatio } from '../configurator/sceneQuality'
import { PRODUCT_LIGHTING } from '../configurator/productLighting'
import { scheduleSceneLoadingLabel } from '../configurator/sceneLoadingState'
import { markingGroupForSourceMesh } from '../configurator/markingDebug'
import {
  cablePoseAt,
  createUsbCable,
  createUsbCableAnimation,
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
let keyLight
let softbox
let accent
let rimLight
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
  }
}
let model
let environmentTarget
let disposeSurface
let disposeRearMarkings
let markingBasePositions = new Map()
const lifetime = createResourceLifetime()

function disposeObject(object) {
  object?.traverse((child) => {
    child.geometry?.dispose?.()
    const materials = Array.isArray(child.material) ? child.material : [child.material]
    materials.filter(Boolean).forEach((material) => material.dispose?.())
  })
}

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

function createPerspectiveSurface(stage) {
  const gridMaterial = new THREE.ShaderMaterial({
    name: 'perspective-line-surface',
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    extensions: { derivatives: true },
    uniforms: {
      horizonFadeEnd: { value: 1.35 },
      horizonFadeStart: { value: 0.58 },
      lineColor: { value: new THREE.Color(0x6f7784) },
      spacing: { value: 0.032 },
      stageFadeEnd: { value: 0.82 },
      stageFadeStart: { value: 0.28 },
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
      uniform float horizonFadeStart;
      uniform float horizonFadeEnd;
      uniform float stageFadeStart;
      uniform float stageFadeEnd;
      varying vec3 vWorldPosition;

      void main() {
        vec2 coordinate = vWorldPosition.xz / spacing;
        vec2 footprint = max(fwidth(coordinate), vec2(0.0001));
        vec2 distanceToLine = abs(fract(coordinate - 0.5) - 0.5) / footprint;
        float line = 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
        float cellSizeInPixels = 1.0 / max(footprint.x, footprint.y);
        float densityFade = smoothstep(3.5, 9.0, cellSizeInPixels);
        float cameraDistance = distance(cameraPosition, vWorldPosition);
        float horizonFade = 1.0 - smoothstep(horizonFadeStart, horizonFadeEnd, cameraDistance);
        float stageFade = 1.0 - smoothstep(stageFadeStart, stageFadeEnd, length(vWorldPosition.xz));

        float surfaceVeil = horizonFade * stageFade * 0.032;
        float gridAlpha = line * densityFade * horizonFade * stageFade * stageFade * 0.24;
        gl_FragColor = vec4(lineColor, surfaceVeil + gridAlpha);
      }
    `,
  })
  const grid = new THREE.Mesh(new THREE.PlaneGeometry(8, 8), gridMaterial)
  grid.name = 'SURFACE_GRID'
  grid.rotation.x = -Math.PI / 2
  grid.position.y = stage.surfaceY
  grid.renderOrder = -1
  return grid
}

function createPhysicalShadowLayer(stage, opacity = 0.28) {
  const material = new THREE.ShadowMaterial({
    color: 0x4d524f,
    opacity,
    transparent: true,
    depthWrite: false,
    toneMapped: false,
  })
  const shadowLayer = new THREE.Mesh(new THREE.PlaneGeometry(0.72, 0.52), material)
  shadowLayer.name = 'PHYSICAL_SHADOW_LAYER'
  shadowLayer.rotation.x = -Math.PI / 2
  shadowLayer.position.y = stage.shadowY
  shadowLayer.receiveShadow = true
  shadowLayer.renderOrder = 0
  return shadowLayer
}

function createContactOcclusion(stage, opacityScale = 1) {
  const material = new THREE.ShaderMaterial({
    name: 'contact-occlusion',
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    uniforms: {
      contactOpacity: { value: stage.contactOpacity * opacityScale },
      contactPower: { value: stage.contactPower },
    },
    vertexShader: `
      varying vec2 vUv;
      void main() {
        vUv = uv;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: `
      varying vec2 vUv;
      uniform float contactOpacity;
      uniform float contactPower;
      void main() {
        float ends = 1.0 - smoothstep(0.462, 0.5, abs(vUv.x - 0.5));
        float frontTail = smoothstep(0.0, 0.5, vUv.y);
        float backTail = 1.0 - smoothstep(0.5, 1.0, vUv.y);
        float contact = vUv.y < 0.5 ? frontTail : backTail;
        contact = pow(max(contact, 0.0), contactPower);
        gl_FragColor = vec4(vec3(0.075, 0.082, 0.078), ends * contact * contactOpacity);
      }
    `,
  })
  const contact = new THREE.Mesh(
    new THREE.PlaneGeometry(stage.contactWidth, stage.contactDepth),
    material,
  )
  contact.name = 'CONTACT_OCCLUSION'
  contact.rotation.x = -Math.PI / 2
  // BODY_03 touches the surface across x ±87.5 mm and z 0…4 mm. Keep the
  // shader's end fade outside that footprint so the shadow reaches beneath
  // both rounded corners instead of disappearing just before them.
  contact.position.set(0, stage.contactY, stage.contactZ)
  contact.renderOrder = 1
  return contact
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

    environmentTarget = createProductStudioEnvironment(renderer, lighting.environment)
    scene.environment = environmentTarget.texture

    scene.add(new THREE.HemisphereLight(
      lighting.hemisphere.sky,
      lighting.hemisphere.ground,
      lighting.hemisphere.intensity,
    ))
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
    const deviceStage = deviceStageForBoard(props.boardId)
    if (!props.captureMode) scene.add(createPerspectiveSurface(deviceStage))

    const loadedModel = await loadDeviceModel(props.boardId)
    if (!lifetime.adopt(loadedModel, disposeObject)) return
    model = loadedModel
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
    const initialConfig = currentDisplayConfig()
    const initialForecast = forecast.value
    const initialForecastRevision = forecastRevision.value
    const loadedScreenSource = await createScreenTexture({
      forecast: initialForecast,
      config: initialConfig,
      boardId: props.boardId,
    })
    if (!lifetime.adopt(loadedScreenSource, (source) => source.dispose())) return
    screenSource = loadedScreenSource
    if (showThreshold.value !== initialConfig.showThreshold || threshold.value !== initialConfig.threshold ||
        showWeather.value !== initialConfig.showWeather ||
        showTemperature.value !== initialConfig.showTemperature ||
        showDedicatedFooter.value !== initialConfig.showDedicatedFooter ||
        timeFormat.value !== initialConfig.timeFormat ||
        temperatureUnit.value !== initialConfig.temperatureUnit ||
        effectiveShowTide.value !== initialConfig.showTide || tide.value !== initialConfig.tide ||
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
      createPhysicalShadowLayer(deviceStage, props.captureMode ? 0.14 : 0.28),
      createContactOcclusion(deviceStage, props.captureMode ? 0.5 : 1),
      model,
      usbCable.object,
    )
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
  reduceMotionQuery = window.matchMedia('(prefers-reduced-motion: reduce)')
  reduceMotionQuery.addEventListener('change', handleReducedMotionChange)
  document.addEventListener('visibilitychange', handleSceneVisibility)
  initialize()
})
onBeforeUnmount(() => {
  lifetime.cancel()
  stopLoadingStatus()
  if (animationFrame !== undefined) cancelAnimationFrame(animationFrame)
  if (viewportResizeFrame !== undefined) cancelAnimationFrame(viewportResizeFrame)
  resizeObserver?.disconnect()
  window.visualViewport?.removeEventListener('resize', scheduleViewportResize)
  window.visualViewport?.removeEventListener('scroll', scheduleViewportResize)
  reduceMotionQuery?.removeEventListener('change', handleReducedMotionChange)
  document.removeEventListener('visibilitychange', handleSceneVisibility)
  orbitRendering?.dispose()
  controls?.dispose()
  screenSource?.dispose()
  disposeSurface?.()
  disposeRearMarkings?.()
  environmentTarget?.dispose()
  keyLight?.shadow.dispose()
  gtaoPass?.dispose()
  smaaPass?.dispose()
  outputPass?.dispose()
  composer?.dispose()
  disposeObject(model)
  disposeObject(scene?.getObjectByName('SURFACE_GRID'))
  disposeObject(scene?.getObjectByName('PHYSICAL_SHADOW_LAYER'))
  disposeObject(scene?.getObjectByName('CONTACT_OCCLUSION'))
  usbCable?.dispose()
  renderer?.dispose()
  renderer?.forceContextLoss()
  renderer?.domElement.remove()
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
    <span v-if="status === 'loading' && showLoadingStatus" class="scene-status" role="status">Building your Windscout…</span>
    <aside v-if="cableLabEnabled" class="cable-lab" aria-label="Cable motion lab">
      <header>
        <strong>Cable motion lab</strong>
        <span>Temporary prototype</span>
      </header>

      <label>
        <span>Route distance <output>{{ cableLab.distance.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.distance" type="range" min="0.35" max="1.8" step="0.05">
      </label>
      <label>
        <span>3D speed <output>{{ cableLab.speed.toFixed(2) }}/s</output></span>
        <input v-model.number="cableLab.speed" type="range" min="0.15" max="0.9" step="0.05">
      </label>
      <p class="cable-lab__duration">Duration <strong>{{ cableLabDuration.toFixed(2) }}s</strong></p>

      <label>
        <span>Shared haze start <output>{{ cableLab.hazeStart.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.hazeStart" type="range" min="0.15" max="1.4" step="0.05">
      </label>
      <label>
        <span>Shared haze end <output>{{ cableLab.hazeEnd.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.hazeEnd" type="range" min="0.25" max="2.5" step="0.05">
      </label>
      <label>
        <span>Grid horizon start <output>{{ cableLab.gridHorizonStart.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.gridHorizonStart" type="range" min="0.4" max="2.5" step="0.1">
      </label>
      <label>
        <span>Grid horizon end <output>{{ cableLab.gridHorizonEnd.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.gridHorizonEnd" type="range" min="0.8" max="5" step="0.1">
      </label>

      <div class="cable-lab__actions">
        <button type="button" @click="runCableLab(true)">Insert</button>
        <button type="button" @click="runCableLab(false)">Retract</button>
        <button type="button" @click="setCableLabCamera('usb')">USB view</button>
        <button type="button" @click="setCableLabCamera('cable')">Cable detail</button>
        <button type="button" @click="setCableLabCamera('hero')">Hero view</button>
      </div>
    </aside>
    <button
      v-if="markingsLabEnabled && boardId === BOARD_IDS.E1002 && !markingsLabOpen"
      type="button"
      class="markings-lab-launch"
      @pointerdown.stop
      @click="markingsLabOpen = true"
    >
      Markings
    </button>
    <aside
      v-if="markingsLabEnabled && boardId === BOARD_IDS.E1002 && markingsLabOpen"
      class="markings-lab"
      aria-label="E1002 marking position controls"
      @pointerdown.stop
    >
      <header>
        <strong>E1002 markings</strong>
        <button type="button" @click="markingsLabOpen = false">Hide</button>
      </header>

      <section v-for="(label, group) in markingGroupLabels" :key="group">
        <strong>{{ label }}</strong>
        <label>
          <span>X</span>
          <input v-model.number="markingOffsets[group].x" type="range" min="-60" max="60" step="0.1">
          <input v-model.number="markingOffsets[group].x" type="number" min="-60" max="60" step="0.1">
        </label>
        <label>
          <span>Y</span>
          <input v-model.number="markingOffsets[group].y" type="range" min="-60" max="60" step="0.1">
          <input v-model.number="markingOffsets[group].y" type="number" min="-60" max="60" step="0.1">
        </label>
      </section>

      <div class="markings-lab__actions">
        <button type="button" @click="showMarkingsRearView">Rear view</button>
        <button type="button" @click="resetMarkingOffsets">Reset</button>
        <button type="button" class="markings-lab__copy" @click="copyMarkingOffsets">
          {{ markingCopyStatus || 'Copy values' }}
        </button>
      </div>
      <textarea
        class="markings-lab__values"
        :value="markingValuesJson"
        readonly
        aria-label="Marking offsets as JSON"
        @focus="$event.target.select()"
      />
    </aside>
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
.cable-lab {
  position: absolute;
  z-index: 10;
  inset: auto auto 1rem 1rem;
  width: min(18rem, calc(100% - 2rem));
  padding: 0.9rem;
  border: 1px solid rgb(255 255 255 / 72%);
  border-radius: 0.9rem;
  background: rgb(245 247 249 / 88%);
  box-shadow: 0 1rem 3rem rgb(31 38 43 / 14%);
  color: #20262b;
  cursor: default;
  backdrop-filter: blur(20px);
  max-height: calc(100% - 2rem);
  overflow: auto;
  scrollbar-width: thin;
}
.cable-lab header {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  align-items: baseline;
  margin-block-end: 0.75rem;
}
.cable-lab header strong { font-size: 0.8rem; }
.cable-lab header span,
.cable-lab__duration { color: #687078; font-size: 0.65rem; }
.cable-lab label { display: grid; gap: 0.2rem; margin-block: 0.55rem; }
.cable-lab label > span {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  font-size: 0.68rem;
}
.cable-lab output { font-family: 'JetBrains Mono Variable', monospace; }
.cable-lab input { width: 100%; accent-color: #70ad32; }
.cable-lab__duration { margin: -0.15rem 0 0.75rem; }
.cable-lab__actions { display: grid; grid-template-columns: 1fr 1fr; gap: 0.4rem; }
.cable-lab button {
  min-height: 2rem;
  border: 1px solid rgb(32 38 43 / 12%);
  border-radius: 0.55rem;
  background: #fff;
  color: inherit;
  font: 600 0.68rem/1 Inter, sans-serif;
  cursor: pointer;
}
.cable-lab button:active { transform: translateY(1px); }
.markings-lab {
  position: absolute;
  z-index: 12;
  inset: 0.75rem auto auto 0.75rem;
  width: min(18rem, calc(100% - 1.5rem));
  max-height: calc(100% - 1.5rem);
  overflow: auto;
  padding: 0.8rem;
  border-radius: 0.9rem;
  background: rgb(247 248 247 / 92%);
  box-shadow: 0 1rem 3rem rgb(20 24 22 / 18%), inset 0 0 0 1px rgb(0 0 0 / 8%);
  color: #202420;
  cursor: default;
  backdrop-filter: blur(18px);
}
.markings-lab header {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 0.75rem;
  margin-block-end: 0.55rem;
}
.markings-lab header strong { font-size: 0.78rem; }
.markings-lab header button {
  min-height: 1.65rem;
  padding-inline: 0.65rem;
  color: #5d635d;
}
.markings-lab section {
  padding-block: 0.5rem;
  border-block-start: 1px solid rgb(0 0 0 / 8%);
}
.markings-lab section > strong { display: block; margin-block-end: 0.35rem; font-size: 0.68rem; }
.markings-lab label {
  display: grid;
  grid-template-columns: 0.8rem 1fr 3.6rem;
  align-items: center;
  gap: 0.4rem;
  min-height: 1.8rem;
  font: 600 0.64rem/1 'JetBrains Mono Variable', monospace;
}
.markings-lab input[type='range'] { width: 100%; accent-color: #70ad32; }
.markings-lab input[type='number'] {
  width: 100%;
  min-height: 1.65rem;
  padding-inline: 0.35rem;
  border: 1px solid rgb(0 0 0 / 12%);
  border-radius: 0.4rem;
  background: #fff;
  color: inherit;
  font: inherit;
  text-align: right;
}
.markings-lab__actions { display: grid; grid-template-columns: 1fr 1fr; gap: 0.4rem; margin-block-start: 0.45rem; }
.markings-lab button {
  min-height: 2rem;
  border: 0;
  border-radius: 0.55rem;
  background: #fff;
  box-shadow: inset 0 0 0 1px rgb(0 0 0 / 10%);
  color: inherit;
  font: 600 0.68rem/1 Inter, sans-serif;
  cursor: pointer;
}
.markings-lab button:active { scale: 0.96; }
.markings-lab__copy { grid-column: 1 / -1; background: #171a17 !important; color: #fff !important; }
.markings-lab__values {
  width: 100%;
  height: 3.25rem;
  margin-block-start: 0.45rem;
  padding: 0.45rem;
  resize: vertical;
  border: 1px solid rgb(0 0 0 / 10%);
  border-radius: 0.5rem;
  background: rgb(255 255 255 / 75%);
  color: #505650;
  font: 500 0.58rem/1.35 'JetBrains Mono Variable', monospace;
}
.markings-lab-launch {
  position: absolute;
  z-index: 12;
  inset: 0.75rem auto auto 0.75rem;
  min-height: 2.25rem;
  padding-inline: 0.85rem;
  border: 0;
  border-radius: 0.65rem;
  background: #171a17;
  box-shadow: 0 0.75rem 2rem rgb(20 24 22 / 20%);
  color: #fff;
  font: 650 0.72rem/1 Inter, sans-serif;
  cursor: pointer;
}
.markings-lab-launch:active { scale: 0.96; }
@media (max-width: 40rem) {
  .markings-lab { max-height: 48%; }
}
</style>
