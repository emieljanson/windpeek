import { BOARD_IDS } from '../config/configuration'

export const HERO_CAMERA = Object.freeze({
  position: Object.freeze([-0.18, 0.09, 0.42]),
  target: Object.freeze([0, 0, 0]),
})

export const NARROW_CAMERA = Object.freeze({
  position: Object.freeze([-0.075, 0.045, 0.67]),
  target: Object.freeze([0, 0, 0]),
})

export const USB_CAMERA = Object.freeze({
  // Look across the right rear corner so the side-mounted USB-C socket and
  // the cable entering behind the front face remain visible together.
  position: Object.freeze([0.28, 0.075, -0.32]),
  target: Object.freeze([0.035, -0.005, -0.004]),
})

export const E1003_USB_CAMERA = Object.freeze({
  // The E1003 socket sits lower on its taller rear pod. Pull back slightly so
  // the larger device and the entering connector remain readable together.
  position: Object.freeze([0.36, 0.055, -0.38]),
  target: Object.freeze([0.058, -0.026, -0.007]),
})

export function usbCameraForBoard(boardId) {
  return boardId === BOARD_IDS.E1003 ? E1003_USB_CAMERA : USB_CAMERA
}

const DEVICE_STAGES = Object.freeze({
  [BOARD_IDS.E1002]: Object.freeze({
    surfaceY: -0.06035,
    shadowY: -0.0604,
    contactY: -0.06012,
    contactWidth: 0.183,
    contactDepth: 0.026,
    contactZ: 0.004,
    contactOpacity: 0.58,
    contactPower: 2.35,
    heroZoom: 1,
  }),
  [BOARD_IDS.E1003]: Object.freeze({
    surfaceY: -0.093,
    shadowY: -0.09305,
    contactY: -0.09277,
    contactWidth: 0.232,
    contactDepth: 0.0285,
    contactZ: 0.0044,
    contactOpacity: 0.22,
    contactPower: 1.2,
    heroZoom: 0.72,
  }),
})

export function deviceStageForBoard(boardId) {
  return DEVICE_STAGES[boardId] ?? DEVICE_STAGES[BOARD_IDS.E1002]
}

export const CABLE_CAMERA = Object.freeze({
  // Product on the left, exposed cable running across the frame on the right.
  // This is a development/detail pose for judging the copper and data effect.
  position: Object.freeze([0.15, 0.105, 0.2]),
  target: Object.freeze([0.12, -0.04, -0.005]),
})

export const USB_CAMERA_DURATION_MS = 1500
export const HERO_ENTRANCE_DURATION_MS = 1800

const HERO_ENTRANCE_AZIMUTH_OFFSET = (Math.PI / 180) * 10
const HERO_ENTRANCE_POLAR_OFFSET = (Math.PI / 180) * 5
const HERO_ENTRANCE_DISTANCE_SCALE = 1.06
const HERO_ENTRANCE_RESPONSE = 5

const CAMERA_VIEWS = Object.freeze({
  cable: CABLE_CAMERA,
  usb: USB_CAMERA,
})

export const ORBIT_LIMITS = Object.freeze({
  // Let the product be inspected from every side and from almost overhead.
  minAzimuth: -Infinity,
  maxAzimuth: Infinity,
  minPolar: 0.25,
  // Keep the camera above the surface so the product always retains a visible
  // ground plane instead of being viewed from underneath against empty space.
  maxPolar: 1.43,
  minDistance: 0.18,
  maxDistance: 0.75,
})

const COMPACT_LAYOUT_MAX_WIDTH = 56 * 16
const COMPACT_STAGE_GAP = 12
const PRODUCT_HEIGHT_RATIO = 0.44
const COMPACT_PRODUCT_FIT_WIDTH = 540
const MIN_COMPOSITION_ZOOM = 0.55

export function calculateSceneComposition({ width, height, settingsTop, panelPlacement = 'auto' }) {
  if (panelPlacement === 'side' || (panelPlacement === 'auto' && width > COMPACT_LAYOUT_MAX_WIDTH)) {
    return {
      availableHeight: height,
      viewOffsetX: Math.round(width * 0.13),
      viewOffsetY: 0,
      zoom: 1,
    }
  }

  const measuredTop = Number.isFinite(settingsTop) ? settingsTop : height
  const availableHeight = Math.max(0, Math.round(Math.min(measuredTop, height) - COMPACT_STAGE_GAP))
  const viewOffsetY = Math.max(0, Math.round((height - availableHeight) / 2))
  const comfortableProductHeight = Math.max(height * PRODUCT_HEIGHT_RATIO, 1)
  const widthZoom = Math.min(1, width / COMPACT_PRODUCT_FIT_WIDTH)
  const zoom = Math.max(
    MIN_COMPOSITION_ZOOM,
    Math.min(widthZoom, availableHeight / comfortableProductHeight),
  )

  return {
    availableHeight,
    viewOffsetX: 0,
    viewOffsetY,
    zoom,
  }
}

export function applyHeroPose(camera, controls, aspect = 1.5, compact = false) {
  const pose = compact || aspect < 0.9 ? NARROW_CAMERA : HERO_CAMERA
  camera.position.set(...pose.position)
  controls.target.set(...pose.target)
  controls.update()
}

export function configureOrbitControls(controls) {
  controls.enablePan = false
  controls.enableDamping = true
  controls.dampingFactor = 0.075
  controls.rotateSpeed = 0.58
  controls.zoomSpeed = 0.7
  controls.minAzimuthAngle = ORBIT_LIMITS.minAzimuth
  controls.maxAzimuthAngle = ORBIT_LIMITS.maxAzimuth
  controls.minPolarAngle = ORBIT_LIMITS.minPolar
  controls.maxPolarAngle = ORBIT_LIMITS.maxPolar
  controls.minDistance = ORBIT_LIMITS.minDistance
  controls.maxDistance = ORBIT_LIMITS.maxDistance
}

export function createOrbitRendering(controls, requestRender) {
  const camera = controls.object
  const position = camera.position.clone()
  const quaternion = camera.quaternion.clone()
  let updating = false
  function onChange() { if (!updating) requestRender() }
  controls.addEventListener('change', onChange)
  return {
    update() {
      position.copy(camera.position)
      quaternion.copy(camera.quaternion)
      const zoom = camera.zoom
      updating = true
      try { controls.update() } finally { updating = false }
      // OrbitControls can report a perpetual zoom change at minDistance due
      // to floating-point rounding. Only keep drawing actual camera movement.
      return position.distanceToSquared(camera.position) > 1e-14
        || 1 - Math.abs(quaternion.dot(camera.quaternion)) > 1e-12
        || Math.abs(zoom - camera.zoom) > 1e-10
    },
    dispose() { controls.removeEventListener('change', onChange) },
  }
}

function vectorValues(vector) {
  return [vector.x, vector.y, vector.z]
}

function capturePose(camera, controls) {
  return {
    position: vectorValues(camera.position),
    target: vectorValues(controls.target),
  }
}

function applyPose(camera, controls, pose) {
  camera.position.set(...pose.position)
  controls.target.set(...pose.target)
}

function cubicBezierCoordinate(time, firstControl, secondControl) {
  const inverse = 1 - time
  return 3 * inverse ** 2 * time * firstControl
    + 3 * inverse * time ** 2 * secondControl
    + time ** 3
}

// Strong ease-out: cubic-bezier(.23, 1, .32, 1). The camera reacts immediately
// to the step change, then spends most of the transition settling at the port.
export function easeCameraMovement(progress) {
  const x = Math.min(Math.max(progress, 0), 1)
  if (x === 0 || x === 1) return x
  let low = 0
  let high = 1
  let time = x
  for (let iteration = 0; iteration < 12; iteration += 1) {
    time = (low + high) / 2
    if (cubicBezierCoordinate(time, 0.23, 0.32) < x) low = time
    else high = time
  }
  return cubicBezierCoordinate(time, 1, 1)
}

// A critically damped response starts at rest and settles without ever
// crossing the final camera position.
export function springCameraMovement(elapsedMs) {
  const elapsedSeconds = Math.max(elapsedMs, 0) / 1000
  return 1 - (1 + HERO_ENTRANCE_RESPONSE * elapsedSeconds)
    * Math.exp(-HERO_ENTRANCE_RESPONSE * elapsedSeconds)
}

function poseDistance(from, to) {
  return Math.max(
    Math.hypot(...from.position.map((value, index) => value - to.position[index])),
    Math.hypot(...from.target.map((value, index) => value - to.target[index])),
  )
}

function orbitForPose(pose) {
  const offset = pose.position.map((value, index) => value - pose.target[index])
  const radius = Math.hypot(...offset)
  return {
    radius,
    polar: Math.acos(Math.min(Math.max(offset[1] / radius, -1), 1)),
    azimuth: Math.atan2(offset[0], offset[2]),
  }
}

function shortestAngleDifference(from, to) {
  return ((to - from + Math.PI) % (Math.PI * 2)) - Math.PI
}

function applyOrbitInterpolation(camera, controls, from, to, startOrbit, targetOrbit, progress) {
  const targetX = from.target[0] + (to.target[0] - from.target[0]) * progress
  const targetY = from.target[1] + (to.target[1] - from.target[1]) * progress
  const targetZ = from.target[2] + (to.target[2] - from.target[2]) * progress
  const radius = startOrbit.radius + (targetOrbit.radius - startOrbit.radius) * progress
  const polar = startOrbit.polar + (targetOrbit.polar - startOrbit.polar) * progress
  const azimuth = startOrbit.azimuth
    + shortestAngleDifference(startOrbit.azimuth, targetOrbit.azimuth) * progress
  const horizontalRadius = radius * Math.sin(polar)

  controls.target.set(targetX, targetY, targetZ)
  camera.position.set(
    targetX + horizontalRadius * Math.sin(azimuth),
    targetY + radius * Math.cos(polar),
    targetZ + horizontalRadius * Math.cos(azimuth),
  )
  camera.lookAt(targetX, targetY, targetZ)
}

export function createHeroEntranceAnimation({
  camera,
  controls,
  reducedMotion,
  requestRender,
}) {
  let targetPose
  let startOrbit
  let targetOrbit
  let animationStart
  let animating = false

  function finish() {
    if (!animating) return
    applyPose(camera, controls, targetPose)
    animating = false
    animationStart = undefined
    controls.enabled = true
    controls.update()
  }

  function start() {
    targetPose = capturePose(camera, controls)
    targetOrbit = orbitForPose(targetPose)
    startOrbit = {
      ...targetOrbit,
      azimuth: targetOrbit.azimuth - HERO_ENTRANCE_AZIMUTH_OFFSET,
      polar: targetOrbit.polar - HERO_ENTRANCE_POLAR_OFFSET,
      radius: targetOrbit.radius * HERO_ENTRANCE_DISTANCE_SCALE,
    }
    if (reducedMotion()) {
      requestRender()
      return false
    }
    applyOrbitInterpolation(camera, controls, targetPose, targetPose, startOrbit, targetOrbit, 0)
    animationStart = undefined
    animating = true
    controls.enabled = false
    requestRender()
    return true
  }

  function update(timestamp) {
    if (!animating) return false
    if (animationStart === undefined) animationStart = timestamp
    const elapsed = timestamp - animationStart
    applyOrbitInterpolation(
      camera,
      controls,
      targetPose,
      targetPose,
      startOrbit,
      targetOrbit,
      springCameraMovement(elapsed),
    )
    if (elapsed >= HERO_ENTRANCE_DURATION_MS) {
      finish()
      return false
    }
    return true
  }

  function finishForReducedMotion() {
    if (!animating) return
    finish()
    requestRender()
  }

  return { finish, finishForReducedMotion, start, update }
}

export function createUsbCameraAnimation({
  camera,
  controls,
  reducedMotion,
  requestRender,
  usbPose = USB_CAMERA,
}) {
  const cameraViews = { ...CAMERA_VIEWS, usb: usbPose }
  const referenceDistance = poseDistance(HERO_CAMERA, usbPose)
  let savedPose
  let startPose
  let startOrbit
  let targetPose
  let targetOrbit
  let animationStart
  let animationDuration = USB_CAMERA_DURATION_MS
  let animating = false
  let activeView = 'hero'

  function finish() {
    applyPose(camera, controls, targetPose)
    animating = false
    animationStart = undefined
    controls.enabled = true
    controls.update()
    if (activeView === 'hero') savedPose = undefined
  }

  function begin(target) {
    startPose = capturePose(camera, controls)
    startOrbit = orbitForPose(startPose)
    targetPose = target
    targetOrbit = orbitForPose(targetPose)
    const distance = poseDistance(startPose, targetPose)
    if (distance < 0.000001 || reducedMotion()) {
      finish()
      requestRender()
      return
    }
    // A reversal starts at the current camera position and takes only as long
    // as its remaining distance needs, with a 200 ms floor for legibility.
    animationDuration = Math.max(200, USB_CAMERA_DURATION_MS * Math.min(distance / referenceDistance, 1))
    animationStart = undefined
    animating = true
    controls.enabled = false
    requestRender()
  }

  function setView(nextView) {
    if (nextView === activeView) return
    if (nextView !== 'hero' && !savedPose) savedPose = capturePose(camera, controls)
    activeView = nextView
    const target = cameraViews[nextView] ?? (savedPose ?? capturePose(camera, controls))
    begin(target)
  }

  function setUsbView(visible) {
    setView(visible ? 'usb' : 'hero')
  }

  function setCableView(visible = true) {
    setView(visible ? 'cable' : 'hero')
  }

  function update(timestamp) {
    if (!animating) return false
    if (animationStart === undefined) animationStart = timestamp
    const progress = Math.min((timestamp - animationStart) / animationDuration, 1)
    applyOrbitInterpolation(
      camera,
      controls,
      startPose,
      targetPose,
      startOrbit,
      targetOrbit,
      easeCameraMovement(progress),
    )
    if (progress === 1) {
      finish()
      return false
    }
    return true
  }

  function finishForReducedMotion() {
    if (!animating) return
    finish()
    requestRender()
  }

  return {
    finishForReducedMotion,
    isAnimating: () => animating,
    setCableView,
    setUsbView,
    update,
  }
}

export function isWebGLAvailable() {
  try {
    const canvas = document.createElement('canvas')
    const context = canvas.getContext('webgl2')
    context?.getExtension('WEBGL_lose_context')?.loseContext()
    return Boolean(context)
  } catch {
    return false
  }
}
