import { filterDetailTexture } from './detailTexture'
import * as THREE from 'three'
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js'
import { BOARD_IDS } from '../config/configuration'
import { WAKE_BUTTON_COLOR } from './productColors'

export const USB_CABLE_COLOR = WAKE_BUTTON_COLOR
export const USB_CABLE_DURATION_MS = 1550
export const USB_CABLE_DISTANCE_FADE_START = 0.5
export const USB_CABLE_DISTANCE_FADE_END = 1.2
export const USB_CABLE_DEFAULT_START_X = 0.56

const DEVICE_REAL_WIDTH_MM = 176
const DEVICE_MODEL_WIDTH = 0.175
const MM_TO_SCENE_UNIT = DEVICE_MODEL_WIDTH / DEVICE_REAL_WIDTH_MM
const millimeters = (value) => value * MM_TO_SCENE_UNIT
const FLOOR_Y = -0.0578
const FLOOR_SURFACE_Y = FLOOR_Y - millimeters(1.45)
const CABLE_OUTER_RADIUS_MM = 2
const LOCAL_FORWARD = new THREE.Vector3(0, 0, 1)
const WORLD_UP = new THREE.Vector3(0, 1, 0)
const VERTICAL_PORT_ROLL = new THREE.Quaternion().setFromAxisAngle(LOCAL_FORWARD, Math.PI / 2)
// The real USB-C socket is vertical in the right side of the E1002 back pod.
// The connector housing stops against that thin side wall while its metal tip
// continues invisibly inside.
const USB_PORT = new THREE.Vector3(0.0437, -0.0071, -0.00535)
const CONNECTOR_HOUSING_FRONT = millimeters(12.4)
const E1002_CABLE_PROFILE = Object.freeze({
  floorY: FLOOR_Y,
  floorSurfaceY: FLOOR_SURFACE_Y,
  housingFront: CONNECTOR_HOUSING_FRONT,
  millimeters,
  port: USB_PORT,
})
const E1003_MM_TO_SCENE_UNIT = 0.001
const E1003_CABLE_PROFILE = Object.freeze({
  floorY: -0.09045,
  floorSurfaceY: -0.09045 - 1.45 * E1003_MM_TO_SCENE_UNIT,
  housingFront: 12.4 * E1003_MM_TO_SCENE_UNIT,
  millimeters: (value) => value * E1003_MM_TO_SCENE_UNIT,
  // Socket centre from STEP meshes 106/107 after the model's y-centering.
  port: new THREE.Vector3(0.0707, -0.02644, -0.00727),
})

function cableProfileForBoard(boardId) {
  return boardId === BOARD_IDS.E1003 ? E1003_CABLE_PROFILE : E1002_CABLE_PROFILE
}

export function applyUsbCableDistanceMask(material) {
  material.transparent = true
  const startUniform = { value: USB_CABLE_DISTANCE_FADE_START }
  const endUniform = { value: USB_CABLE_DISTANCE_FADE_END }
  material.userData.usbCableDistanceMask = {
    startUniform,
    endUniform,
  }
  material.onBeforeCompile = (shader) => {
    shader.uniforms.usbCableFadeStart = startUniform
    shader.uniforms.usbCableFadeEnd = endUniform
    shader.vertexShader = shader.vertexShader
      .replace(
        '#include <common>',
        '#include <common>\nvarying vec3 vUsbCableWorldPosition;',
      )
      .replace(
        '#include <worldpos_vertex>',
        '#include <worldpos_vertex>\nvUsbCableWorldPosition = (modelMatrix * vec4(transformed, 1.0)).xyz;',
      )
    shader.fragmentShader = shader.fragmentShader
      .replace(
        '#include <common>',
        '#include <common>\nvarying vec3 vUsbCableWorldPosition;\nuniform float usbCableFadeStart;\nuniform float usbCableFadeEnd;',
      )
      .replace(
        '#include <opaque_fragment>',
        `float usbCableDistance = length(vUsbCableWorldPosition.xz);
float usbCableDistanceMask = 1.0 - smoothstep(usbCableFadeStart, usbCableFadeEnd, usbCableDistance);
if (usbCableDistanceMask <= 0.001) discard;
diffuseColor.a *= usbCableDistanceMask;
#include <opaque_fragment>`,
      )
  }
  material.customProgramCacheKey = () => 'usb-cable-distance-mask-v1'
}

function createConnectorPath(connectorStartX, profile) {
  const insertedOriginX = profile.port.x + profile.housingFront
  const pathPoints = [0.56, 0.48, 0.4, 0.32, 0.24, 0.16, 0.09, insertedOriginX]
  const scale = (connectorStartX - insertedOriginX)
    / (USB_CABLE_DEFAULT_START_X - insertedOriginX)
  return new THREE.CatmullRomCurve3(
    pathPoints.map((x, index) => new THREE.Vector3(
      index === pathPoints.length - 1
        ? x
        : insertedOriginX + (x - insertedOriginX) * scale,
      profile.port.y,
      profile.port.z,
    )),
    false,
    'centripetal',
  )
}

class SepticBezierCurve3 extends THREE.Curve {
  constructor(...points) {
    super()
    this.points = points
  }

  getPoint(t, target = new THREE.Vector3()) {
    const inverse = 1 - t
    const weights = [
      inverse ** 7,
      7 * inverse ** 6 * t,
      21 * inverse ** 5 * t ** 2,
      35 * inverse ** 4 * t ** 3,
      35 * inverse ** 3 * t ** 4,
      21 * inverse ** 2 * t ** 5,
      7 * inverse * t ** 6,
      t ** 7,
    ]
    target.set(0, 0, 0)
    this.points.forEach((point, index) => target.addScaledVector(point, weights[index]))
    return target
  }
}

// Strong ease-out: movement starts immediately and then settles naturally as
// the connector reaches the socket. This evaluates cubic-bezier(.23, 1, .32, 1).
function easeStrongOut(progress) {
  const x = THREE.MathUtils.clamp(progress, 0, 1)
  if (x === 0 || x === 1) return x

  const coordinate = (time, firstControl, secondControl) => {
    const inverse = 1 - time
    return 3 * inverse ** 2 * time * firstControl
      + 3 * inverse * time ** 2 * secondControl
      + time ** 3
  }

  let low = 0
  let high = 1
  let time = x
  for (let iteration = 0; iteration < 12; iteration += 1) {
    time = (low + high) / 2
    if (coordinate(time, 0.23, 0.32) < x) low = time
    else high = time
  }
  return coordinate(time, 1, 1)
}

export function easeUsbCableProgress(progress) {
  return easeStrongOut(progress)
}

function connectorPositionAt(path, progress) {
  const value = THREE.MathUtils.clamp(progress, 0, 1)
  return path.getPointAt(value)
}

export function cablePoseAt(
  progress,
  compositionMode = 'compact',
  connectorStartX = USB_CABLE_DEFAULT_START_X,
  boardId = BOARD_IDS.E1002,
) {
  const profile = cableProfileForBoard(boardId)
  const value = THREE.MathUtils.clamp(progress, 0, 1)
  const connectorPath = createConnectorPath(connectorStartX, profile)
  const connector = connectorPositionAt(connectorPath, value)
  const tangent = connectorPath.getTangentAt(value)
  // Carry the braid over the narrow strain-relief section and stop it at the
  // wider collar, without letting the woven tube poke through that transition.
  const cableJoin = connector.clone().addScaledVector(tangent, -millimeters(3.6))
  const straightSheathLead = connector.clone().addScaledVector(tangent, -millimeters(36.2))
  const floorDepth = profile.port.z
  // Give the braided sheath enough run-up to lift as one broad, weighted arc.
  // A short run-up makes it behave like soft string and creates a sudden drop
  // immediately behind the connector.
  const floorApproachX = Math.max(connector.x + profile.millimeters(105), 0.135)

  return {
    connector,
    tangent,
    cablePoints: [
      new THREE.Vector3(1.6, profile.floorY, floorDepth),
      new THREE.Vector3(1.1, profile.floorY, floorDepth),
      new THREE.Vector3(0.75, profile.floorY, floorDepth),
      new THREE.Vector3(floorApproachX, profile.floorY, floorDepth),
      straightSheathLead,
      cableJoin,
    ],
  }
}

function createBraidTexture(size = 512, {
  baseBrightness = 0.84,
  color = USB_CABLE_COLOR,
  contrast = 0.12,
  repeatX = 180,
  repeatY = 3,
  saturation = 1,
} = {}) {
  const pixels = new Uint8Array(size * size * 4)
  const normalPixels = new Uint8Array(size * size * 4)
  const roughnessPixels = new Uint8Array(size * size * 4)
  const heights = new Float32Array(size * size)
  const channels = [
    (color >> 16) & 0xff,
    (color >> 8) & 0xff,
    color & 0xff,
  ]

  const sampleWeave = (x, y, sample) => {
    const u = x / size
    const v = y / size
    const phaseA = Math.cos(Math.PI * 2 * (u * 6 + v * 4))
    const phaseB = Math.cos(Math.PI * 2 * (u * 6 - v * 4 + 0.5))
    const strandA = Math.max(0, phaseA) ** 2.8
    const strandB = Math.max(0, phaseB) ** 2.8
    const crossing = Math.sin(Math.PI * 2 * u * 12) * Math.sin(Math.PI * 2 * v * 8)
    const upper = crossing >= 0 ? strandA : strandB
    const lower = crossing >= 0 ? strandB : strandA
    const bundle = Math.max(upper, lower * 0.46)
    const fibre = Math.sin(Math.PI * 2 * (u * 156 + v * 11)) * 0.028 * bundle
    const groove = 1 - THREE.MathUtils.clamp(Math.max(strandA, strandB) * 1.35, 0, 1)
    sample.groove = groove
    sample.height = THREE.MathUtils.clamp(bundle + fibre, 0, 1)
  }

  const normal = new THREE.Vector3()
  const weave = { groove: 0, height: 0 }
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const u = x / size
      const v = y / size
      sampleWeave(x, y, weave)
      const colourFibre = Math.sin(Math.PI * 2 * (u * 137 - v * 9)) * 0.018 * weave.height
      const brightness = baseBrightness
        + weave.height * contrast
        - weave.groove * 0.055
        + colourFibre
      const offset = (y * size + x) * 4
      const average = (channels[0] + channels[1] + channels[2]) / 3
      pixels[offset] = Math.min(255, Math.round((average + (channels[0] - average) * saturation) * brightness))
      pixels[offset + 1] = Math.min(255, Math.round((average + (channels[1] - average) * saturation) * brightness))
      pixels[offset + 2] = Math.min(255, Math.round((average + (channels[2] - average) * saturation) * brightness))
      pixels[offset + 3] = 255
      heights[y * size + x] = weave.height
      const roughness = Math.round(244 - weave.height * 34 + weave.groove * 7)
      roughnessPixels[offset] = roughness
      roughnessPixels[offset + 1] = roughness
      roughnessPixels[offset + 2] = roughness
      roughnessPixels[offset + 3] = 255
    }
  }

  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const left = heights[y * size + ((x - 1 + size) % size)]
      const right = heights[y * size + ((x + 1) % size)]
      const down = heights[((y - 1 + size) % size) * size + x]
      const up = heights[((y + 1) % size) * size + x]
      normal.set((left - right) * 2.35, (down - up) * 2.35, 1).normalize()
      const offset = (y * size + x) * 4
      normalPixels[offset] = Math.round((normal.x * 0.5 + 0.5) * 255)
      normalPixels[offset + 1] = Math.round((normal.y * 0.5 + 0.5) * 255)
      normalPixels[offset + 2] = Math.round((normal.z * 0.5 + 0.5) * 255)
      normalPixels[offset + 3] = 255
    }
  }

  const texture = new THREE.DataTexture(pixels, size, size, THREE.RGBAFormat)
  texture.name = 'usb-cable-braid-texture'
  texture.colorSpace = THREE.SRGBColorSpace
  texture.wrapS = THREE.RepeatWrapping
  texture.wrapT = THREE.RepeatWrapping
  texture.repeat.set(repeatX, repeatY)
  texture.anisotropy = 16

  const normalTexture = new THREE.DataTexture(normalPixels, size, size, THREE.RGBAFormat)
  normalTexture.name = 'usb-cable-braid-normal-texture'
  normalTexture.wrapS = THREE.RepeatWrapping
  normalTexture.wrapT = THREE.RepeatWrapping
  normalTexture.repeat.copy(texture.repeat)
  normalTexture.anisotropy = texture.anisotropy

  const roughnessTexture = new THREE.DataTexture(roughnessPixels, size, size, THREE.RGBAFormat)
  roughnessTexture.name = 'usb-cable-braid-roughness-texture'
  roughnessTexture.wrapS = THREE.RepeatWrapping
  roughnessTexture.wrapT = THREE.RepeatWrapping
  roughnessTexture.repeat.copy(texture.repeat)
  roughnessTexture.anisotropy = texture.anisotropy
  return { color: filterDetailTexture(texture), normal: filterDetailTexture(normalTexture), roughness: filterDetailTexture(roughnessTexture) }
}

function createMoldGrainTexture(size = 128) {
  const heights = new Float32Array(size * size)
  const normalPixels = new Uint8Array(size * size * 4)
  const roughnessPixels = new Uint8Array(size * size * 4)
  const hash = (x, y) => {
    const value = Math.sin(x * 127.1 + y * 311.7) * 43758.5453
    return value - Math.floor(value)
  }

  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const fine = hash(x, y)
      const broad = hash(Math.floor(x / 3), Math.floor(y / 3))
      heights[y * size + x] = fine * 0.62 + broad * 0.38
    }
  }

  const sample = (sampleX, sampleY) => heights[
    ((sampleY + size) % size) * size + ((sampleX + size) % size)
  ]
  const normal = new THREE.Vector3()
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      normal.set(
        (sample(x - 1, y) - sample(x + 1, y)) * 0.32,
        (sample(x, y - 1) - sample(x, y + 1)) * 0.32,
        1,
      ).normalize()
      const offset = (y * size + x) * 4
      normalPixels[offset] = Math.round((normal.x * 0.5 + 0.5) * 255)
      normalPixels[offset + 1] = Math.round((normal.y * 0.5 + 0.5) * 255)
      normalPixels[offset + 2] = Math.round((normal.z * 0.5 + 0.5) * 255)
      normalPixels[offset + 3] = 255
      const roughness = Math.round(220 + (heights[y * size + x] - 0.5) * 18)
      roughnessPixels[offset] = roughness
      roughnessPixels[offset + 1] = roughness
      roughnessPixels[offset + 2] = roughness
      roughnessPixels[offset + 3] = 255
    }
  }

  const createTexture = (data, name) => {
    const texture = new THREE.DataTexture(data, size, size, THREE.RGBAFormat)
    texture.name = name
    texture.wrapS = THREE.RepeatWrapping
    texture.wrapT = THREE.RepeatWrapping
    texture.repeat.set(6, 6)
    texture.anisotropy = 8
    return filterDetailTexture(texture)
  }

  return {
    normal: createTexture(normalPixels, 'usb-connector-mold-normal-texture'),
    roughness: createTexture(roughnessPixels, 'usb-connector-mold-roughness-texture'),
  }
}

function createCableFadeTexture(size = 256) {
  const pixels = new Uint8Array(size * 4)
  for (let x = 0; x < size; x += 1) {
    const u = x / (size - 1)
    const progress = THREE.MathUtils.clamp((u - 0.02) / 0.22, 0, 1)
    const opacity = Math.round((progress * progress * (3 - 2 * progress)) * 255)
    const offset = x * 4
    pixels[offset] = opacity
    pixels[offset + 1] = opacity
    pixels[offset + 2] = opacity
    pixels[offset + 3] = 255
  }

  const texture = new THREE.DataTexture(pixels, size, 1, THREE.RGBAFormat)
  texture.name = 'usb-cable-tail-fade-texture'
  texture.wrapS = THREE.ClampToEdgeWrapping
  texture.wrapT = THREE.ClampToEdgeWrapping
  texture.minFilter = THREE.LinearFilter
  texture.magFilter = THREE.LinearFilter
  texture.needsUpdate = true
  return texture
}

function createContactShadowFadeTexture(size = 256) {
  const pixels = new Uint8Array(size * 4)
  for (let x = 0; x < size; x += 1) {
    const u = x / (size - 1)
    const fadeProgress = THREE.MathUtils.clamp(u / 0.72, 0, 1)
    const smoothFade = fadeProgress * fadeProgress * (3 - 2 * fadeProgress)
    const opacity = Math.round(smoothFade * 255)
    const offset = x * 4
    pixels[offset] = opacity
    pixels[offset + 1] = opacity
    pixels[offset + 2] = opacity
    pixels[offset + 3] = 255
  }

  const texture = new THREE.DataTexture(pixels, size, 1, THREE.RGBAFormat)
  texture.name = 'usb-cable-contact-shadow-fade-texture'
  texture.wrapS = THREE.ClampToEdgeWrapping
  texture.wrapT = THREE.ClampToEdgeWrapping
  texture.minFilter = THREE.LinearFilter
  texture.magFilter = THREE.LinearFilter
  texture.needsUpdate = true
  return texture
}

function createConnector(materials) {
  const connector = new THREE.Group()
  connector.name = 'USB_C_CONNECTOR'
  // The opacity-controlled contact layer owns every cable shadow so the cable
  // and its grounding appear and disappear as one visual unit.

  const housingGeometry = new RoundedBoxGeometry(
    millimeters(9.2),
    millimeters(4.3),
    millimeters(13),
    8,
    millimeters(0.96),
  )

  const housing = new THREE.Mesh(housingGeometry, materials.housing)
  housing.name = 'USB_C_CONNECTOR_HOUSING'
  housing.position.z = millimeters(5.9)

  const tip = new THREE.Mesh(
    new RoundedBoxGeometry(millimeters(8.25), millimeters(2.4), millimeters(6.65), 4, millimeters(1.1)),
    materials.metal,
  )
  tip.name = 'USB_C_CONNECTOR_TIP'
  tip.position.z = millimeters(15.725)

  const opening = new THREE.Mesh(
    new RoundedBoxGeometry(millimeters(6.5), millimeters(1.35), millimeters(0.24), 3, millimeters(0.55)),
    materials.port,
  )
  opening.name = 'USB_C_CONNECTOR_OPENING'
  opening.position.z = millimeters(19.17)

  const tongue = new THREE.Mesh(
    new RoundedBoxGeometry(millimeters(4.8), millimeters(0.42), millimeters(0.16), 2, millimeters(0.18)),
    materials.tongue,
  )
  tongue.name = 'USB_C_CONNECTOR_TONGUE'
  tongue.position.y = millimeters(-0.2)
  tongue.position.z = millimeters(19.34)

  connector.add(housing, tip, opening, tongue)
  const reliefProfile = [
    [1.74, -7.4],
    [1.76, -6.55],
    [1.82, -5.8],
    [1.91, -5.2],
    [1.86, -4.7],
    [1.98, -4.15],
    [2.16, -3.68],
    [2.14, -3.42],
    [2.04, -3.08],
    [2.11, -2.45],
    [2.15, -1.45],
    [2.1, -0.58],
  ].map(([radius, z]) => new THREE.Vector2(millimeters(radius), millimeters(z)))
  const relief = new THREE.Mesh(
    new THREE.LatheGeometry(reliefProfile, 48),
    materials.relief,
  )
  relief.name = 'USB_C_STRAIN_RELIEF'
  relief.rotation.x = Math.PI / 2
  connector.add(relief)

  return connector
}

export function cableCurveForPoints(points) {
  if (points.length !== 6) {
    return new THREE.CatmullRomCurve3(points, false, 'centripetal')
  }

  // Apple-like restraint: one calm floor line and one continuous bend into the
  // strain relief. The final controls share the connector tangent, so the cable
  // starts yielding immediately but builds curvature gradually like stiff braid.
  const floorStart = points[0]
  const bendStart = points[3]
  const straightStart = points[4]
  const cableJoin = points.at(-1)
  const bendDistance = bendStart.distanceTo(cableJoin)
  const floorHandleLength = Math.min(millimeters(45), bendDistance * 0.36)
  const floorShoulderLength = Math.min(millimeters(72), bendDistance * 0.58)
  const bendDirectionX = Math.sign(straightStart.x - bendStart.x) || 1
  const connectorDirection = cableJoin.clone().sub(straightStart).normalize()
  const connectorShoulder = straightStart.clone()
    .add(new THREE.Vector3(-bendDirectionX * millimeters(24), 0, 0))
  const connectorControlThree = cableJoin.clone()
    .addScaledVector(connectorDirection, -millimeters(20))
  const connectorControlFour = cableJoin.clone()
    .addScaledVector(connectorDirection, -millimeters(10))
  const bendCurve = new SepticBezierCurve3(
    bendStart,
    bendStart.clone().add(new THREE.Vector3(bendDirectionX * floorHandleLength, 0, 0)),
    bendStart.clone().add(new THREE.Vector3(bendDirectionX * floorShoulderLength, 0, 0)),
    connectorShoulder,
    straightStart,
    connectorControlThree,
    connectorControlFour,
    cableJoin,
  )
  const curve = new THREE.CurvePath()
  curve.add(new THREE.LineCurve3(floorStart, bendStart))
  curve.add(bendCurve)
  return curve
}

function geometryForCable(points, radius = millimeters(CABLE_OUTER_RADIUS_MM)) {
  const curve = cableCurveForPoints(points)
  // Fine longitudinal tessellation keeps tight floor bends round even in a
  // close orbit; the previous 72 segments exposed small direction facets.
  return new THREE.TubeGeometry(curve, 360, radius, 12, false)
}

function updateTubeGeometry(geometry, curve) {
  const {
    tubularSegments,
    radius,
    radialSegments,
    closed,
  } = geometry.parameters
  const frames = curve.computeFrenetFrames(tubularSegments, closed)
  const positions = geometry.attributes.position
  const normals = geometry.attributes.normal
  const point = new THREE.Vector3()
  let vertexIndex = 0

  for (let segment = 0; segment <= tubularSegments; segment += 1) {
    curve.getPointAt(segment / tubularSegments, point)
    const frameNormal = frames.normals[segment]
    const frameBinormal = frames.binormals[segment]
    for (let side = 0; side <= radialSegments; side += 1) {
      const angle = side / radialSegments * Math.PI * 2
      const sin = Math.sin(angle)
      const cos = -Math.cos(angle)
      const normalX = cos * frameNormal.x + sin * frameBinormal.x
      const normalY = cos * frameNormal.y + sin * frameBinormal.y
      const normalZ = cos * frameNormal.z + sin * frameBinormal.z
      normals.setXYZ(vertexIndex, normalX, normalY, normalZ)
      positions.setXYZ(
        vertexIndex,
        point.x + radius * normalX,
        point.y + radius * normalY,
        point.z + radius * normalZ,
      )
      vertexIndex += 1
    }
  }

  geometry.parameters.path = curve
  geometry.tangents = frames.tangents
  geometry.normals = frames.normals
  geometry.binormals = frames.binormals
  positions.needsUpdate = true
  normals.needsUpdate = true
  geometry.computeBoundingSphere()
}

function updateCableGeometry(geometry, points) {
  updateTubeGeometry(geometry, cableCurveForPoints(points))
}

function geometryForFloorEffect(points, {
  pointCount,
  radius,
  verticalScale,
  lift,
  depthOffset,
  profile,
}) {
  const geometry = geometryForCable(points.slice(0, pointCount), millimeters(radius))
  const positions = geometry.attributes.position
  for (let index = 0; index < positions.count; index += 1) {
    positions.setY(
      index,
      profile.floorSurfaceY
        + (positions.getY(index) - profile.floorY) * verticalScale
        + profile.millimeters(lift),
    )
    positions.setZ(index, positions.getZ(index) + profile.millimeters(depthOffset))
  }
  positions.needsUpdate = true
  geometry.computeVertexNormals()
  return geometry
}

function updateFloorEffectGeometry(geometry, points, {
  pointCount,
  verticalScale,
  lift,
  depthOffset,
  profile,
}) {
  updateTubeGeometry(geometry, cableCurveForPoints(points.slice(0, pointCount)))
  const positions = geometry.attributes.position
  for (let index = 0; index < positions.count; index += 1) {
    positions.setY(
      index,
      profile.floorSurfaceY
        + (positions.getY(index) - profile.floorY) * verticalScale
        + profile.millimeters(lift),
    )
    positions.setZ(index, positions.getZ(index) + profile.millimeters(depthOffset))
  }
  positions.needsUpdate = true
  geometry.computeVertexNormals()
  geometry.computeBoundingSphere()
}

function geometryForContactShadow(points, profile) {
  // Keep the crisp contact layer under the floor section only. Flattening the
  // raised bend created a second cable-shaped line beneath the real cable.
  return geometryForFloorEffect(points.slice(2, 4), {
    pointCount: 2,
    radius: 1.78,
    verticalScale: 0.025,
    lift: 0.05,
    depthOffset: 0,
    profile,
  })
}

function updateContactShadowGeometry(geometry, points, profile) {
  updateFloorEffectGeometry(geometry, points.slice(2, 4), {
    pointCount: 2,
    verticalScale: 0.025,
    lift: 0.05,
    depthOffset: 0,
    profile,
  })
}

function orientConnector(connector, tangent) {
  // Keep the plug's thin side vertical while its forward axis follows the
  // insertion path, so it stays vertical while entering the side-facing
  // USB-C socket from the right.
  const forward = tangent.clone().normalize()
  const right = new THREE.Vector3().crossVectors(WORLD_UP, forward).normalize()
  const up = new THREE.Vector3().crossVectors(forward, right).normalize()
  connector.quaternion
    .setFromRotationMatrix(new THREE.Matrix4().makeBasis(right, up, forward))
    .multiply(VERTICAL_PORT_ROLL)
}

export function createUsbCable(initialCompositionMode = 'compact', boardId = BOARD_IDS.E1002) {
  const profile = cableProfileForBoard(boardId)
  let compositionMode = initialCompositionMode
  let currentProgress = 0
  let connectorStartX = USB_CABLE_DEFAULT_START_X
  const braidTextures = createBraidTexture()
  const moldTextures = createMoldGrainTexture()
  const cableFadeTexture = createCableFadeTexture()
  const contactShadowFadeTexture = createContactShadowFadeTexture()
  const materials = {
    braid: new THREE.MeshPhysicalMaterial({
      name: 'usb-cable-braid',
      map: braidTextures.color,
      alphaMap: cableFadeTexture,
      transparent: true,
      normalMap: braidTextures.normal,
      normalScale: new THREE.Vector2(0.52, 0.52),
      roughnessMap: braidTextures.roughness,
      roughness: 0.9,
      metalness: 0,
      clearcoat: 0,
      sheen: 0.14,
      sheenColor: 0xb6e887,
      sheenRoughness: 0.84,
      anisotropy: 0,
      envMapIntensity: 0.52,
    }),
    housing: new THREE.MeshPhysicalMaterial({
      name: 'usb-cable-housing',
      color: 0x659b34,
      roughnessMap: moldTextures.roughness,
      roughness: 0.44,
      metalness: 0,
      clearcoat: 0.02,
      clearcoatRoughness: 0.82,
      ior: 1.46,
      specularIntensity: 0.42,
      normalMap: moldTextures.normal,
      normalScale: new THREE.Vector2(0.16, 0.16),
      envMapIntensity: 0.96,
    }),
    relief: new THREE.MeshPhysicalMaterial({
      name: 'usb-cable-relief',
      color: 0x5d8e31,
      roughnessMap: moldTextures.roughness,
      roughness: 0.7,
      metalness: 0,
      clearcoat: 0,
      sheen: 0.08,
      sheenColor: 0x8cc05e,
      sheenRoughness: 0.96,
      normalMap: moldTextures.normal,
      normalScale: new THREE.Vector2(0.18, 0.18),
      envMapIntensity: 0.62,
    }),
    metal: new THREE.MeshPhysicalMaterial({
      name: 'usb-cable-metal',
      color: 0xc9cdca,
      roughness: 0.22,
      metalness: 1,
      envMapIntensity: 1.35,
    }),
    tongue: new THREE.MeshStandardMaterial({
      name: 'usb-cable-tongue',
      color: 0x526055,
      roughness: 0.68,
      metalness: 0.05,
    }),
    port: new THREE.MeshStandardMaterial({
      name: 'usb-cable-port',
      color: 0x171a18,
      roughness: 0.74,
      metalness: 0,
    }),
    shadow: new THREE.MeshBasicMaterial({
      name: 'usb-cable-contact-shadow',
      color: 0x35403b,
      alphaMap: contactShadowFadeTexture,
      transparent: true,
      opacity: 0.085,
      depthWrite: false,
      side: THREE.DoubleSide,
    }),
  }
  Object.values(materials).forEach(applyUsbCableDistanceMask)
  const materialAppearance = new Map(
    Object.values(materials).map((material) => [material, {
      opacity: material.opacity,
      transparent: material.transparent,
    }]),
  )

  const object = new THREE.Group()
  object.name = 'USB_CABLE'
  object.visible = false

  const initialPose = cablePoseAt(0, compositionMode, connectorStartX, boardId)
  const contactShadow = new THREE.Mesh(
    geometryForContactShadow(initialPose.cablePoints, profile),
    materials.shadow,
  )
  contactShadow.name = 'USB_CABLE_CONTACT_SHADOW'
  contactShadow.renderOrder = -1
  object.add(contactShadow)

  const sheath = new THREE.Mesh(geometryForCable(initialPose.cablePoints), materials.braid)
  sheath.name = 'USB_CABLE_SHEATH'
  // The studio key casts a long shadow behind the rising cable at this scale.
  // Ground the cable with the controlled floor layers instead, so the contact
  // stays visually underneath it from the full orbit range.
  sheath.castShadow = false
  sheath.receiveShadow = true
  object.add(sheath)

  const connector = createConnector(materials)
  object.add(connector)
  connector.position.copy(initialPose.connector)
  orientConnector(connector, initialPose.tangent)
  let cableGeometryProgress = 0

  function setProgress(progress, forceGeometry = false) {
    currentProgress = progress
    const pose = cablePoseAt(progress, compositionMode, connectorStartX, boardId)
    // Follow the easing's exact progress. Quantizing this value made the cable
    // hold a shape for several frames and then jump near the end of an ease-out.
    if (forceGeometry || progress !== cableGeometryProgress) {
      updateCableGeometry(sheath.geometry, pose.cablePoints)
      updateContactShadowGeometry(contactShadow.geometry, pose.cablePoints, profile)
      cableGeometryProgress = progress
    }
    connector.position.copy(pose.connector)
    orientConnector(connector, pose.tangent)
  }

  function setOpacity(opacity) {
    const value = THREE.MathUtils.clamp(opacity, 0, 1)
    materialAppearance.forEach((appearance, material) => {
      const transparent = appearance.transparent || value < 1
      // The braid's alpha texture makes it visually disappear sooner than a
      // solid dark layer at the same numeric opacity. Let the contact shadow
      // trail the cable opacity quadratically so it never outlives the cable.
      const fadeValue = material === materials.shadow ? value ** 2 : value
      material.opacity = appearance.opacity * fadeValue
      if (material.transparent !== transparent) {
        material.transparent = transparent
        material.needsUpdate = true
      }
    })
  }

  return {
    object,
    setOpacity,
    setProgress,
    setDistanceFade(start, end) {
      Object.values(materials).forEach((material) => {
        const mask = material.userData.usbCableDistanceMask
        if (!mask) return
        mask.startUniform.value = start
        mask.endUniform.value = end
      })
    },
    setTravelStartX(nextStartX) {
      connectorStartX = nextStartX
      setProgress(currentProgress, true)
    },
    setCompositionMode(nextCompositionMode) {
      if (nextCompositionMode === compositionMode) return
      compositionMode = nextCompositionMode
      setProgress(currentProgress, true)
    },
    dispose() {
      const geometries = new Set()
      object.traverse((child) => { if (child.geometry) geometries.add(child.geometry) })
      geometries.forEach((geometry) => geometry.dispose())
      Object.values(materials).forEach((material) => material.dispose())
      Object.values(braidTextures).forEach((texture) => texture.dispose())
      Object.values(moldTextures).forEach((texture) => texture.dispose())
      cableFadeTexture.dispose()
      contactShadowFadeTexture.dispose()
    },
  }
}

export function createUsbCableAnimation({ cable, reducedMotion, requestRender }) {
  let animationStart
  let startProgress = 0
  let currentProgress = 0
  let targetVisible = false
  let duration = USB_CABLE_DURATION_MS
  let motionDuration = 0

  function finishAtTarget() {
    const target = Number(targetVisible)
    animationStart = undefined
    currentProgress = target
    cable.setProgress(currentProgress)
    cable.setOpacity(target)
    cable.object.visible = targetVisible
  }

  function setVisible(visible, timestamp = performance.now()) {
    if (visible === targetVisible) return
    if (animationStart !== undefined) update(timestamp)
    targetVisible = visible
    if (reducedMotion()) {
      finishAtTarget()
      requestRender()
      return
    }

    startProgress = currentProgress
    const target = Number(visible)
    motionDuration = Math.abs(target - startProgress) * duration
    if (motionDuration === 0) finishAtTarget()
    else {
      cable.object.visible = true
      cable.setProgress(currentProgress)
      cable.setOpacity(1)
      animationStart = timestamp
    }
    requestRender()
  }

  function update(timestamp) {
    if (animationStart === undefined || reducedMotion()) return false
    const elapsed = timestamp - animationStart
    const motionProgress = motionDuration === 0 ? 1 : Math.min(1, elapsed / motionDuration)
    const target = Number(targetVisible)
    currentProgress = THREE.MathUtils.lerp(
      startProgress,
      target,
      easeUsbCableProgress(motionProgress),
    )
    if (motionProgress === 1) {
      finishAtTarget()
      return false
    }
    cable.setOpacity(1)
    cable.setProgress(currentProgress)
    return true
  }

  function finishForReducedMotion() {
    if (animationStart === undefined) return
    finishAtTarget()
    requestRender()
  }

  return {
    finishForReducedMotion,
    setDuration(nextDuration) { duration = Math.max(1, nextDuration) },
    setVisible,
    update,
  }
}
