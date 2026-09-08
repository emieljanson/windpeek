import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { createRequire } from 'node:module'
import * as THREE from 'three'
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js'
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js'
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js'
import { toCreasedNormals } from 'three/examples/jsm/utils/BufferGeometryUtils.js'
import { E1003_MODEL } from '../src/assets/e1003.js'
import { WAKE_BUTTON_COLOR } from '../src/configurator/productColors.js'

const require = createRequire(import.meta.url)
const projectRoot = dirname(dirname(fileURLToPath(import.meta.url)))
const outputDirectory = join(projectRoot, 'public', 'devices', 'e1003')
const modelPath = join(outputDirectory, 'e1003.glb')
const provenancePath = join(outputDirectory, 'provenance.json')

// Keep the complete outward-facing product while excluding the battery, PCB,
// screen controller, foam and other parts that can never be seen in the viewer.
const meshRoles = Object.freeze({
  BODY: new Set([0, 25, 32, 39, 41, 42]),
  CONTROLS: new Set([14, 33]),
  PORTS: new Set([17, 83, 84, 85, 92, 93, 100, 101, 102, 103, 104, 105, 106, 107]),
  FASTENERS: new Set([35, 36, 37, 38]),
  MARKINGS: new Set(Array.from({ length: 35 }, (_, offset) => 241 + offset)),
})

const creasedMeshes = new Set([0, 25, 32, 39, 41, 42])
const frontGlassMeshes = new Set([39])
const frontTrimMeshes = new Set([41])
const rearCoverMeshes = new Set([32])
const threadedInsertDefinitions = Object.freeze([
  Object.freeze({ sourceMesh: 27, centerMm: Object.freeze([37.5, -38.4]) }),
  Object.freeze({ sourceMesh: 28, centerMm: Object.freeze([-37.5, 36.6]) }),
  Object.freeze({ sourceMesh: 29, centerMm: Object.freeze([37.5, 36.6]) }),
  Object.freeze({ sourceMesh: 30, centerMm: Object.freeze([-37.5, -38.4]) }),
])
const controlHousingMeshes = new Set([33])
const frontLowerCoverMeshes = new Set([42])
const powerSwitchMeshes = new Set([14])
const expansionPortMeshes = new Set([17])
const microSdMeshes = new Set([83])
const ledGreenMeshes = new Set([84, 85])
const ledRedMeshes = new Set([92, 93])
const usbMetalMeshes = new Set([100, 102, 104, 105, 106])
const usbDarkMeshes = new Set([101, 103, 107])

function hash(content) {
  return createHash('sha256').update(content).digest('hex')
}

function roleForMesh(index) {
  return Object.entries(meshRoles).find(([, indexes]) => indexes.has(index))?.[0]
}

function geometryFromOcct(mesh, { creased = false } = {}) {
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(mesh.attributes.position.array.flat(), 3))
  geometry.setIndex(mesh.index.array.flat())
  if (mesh.attributes.normal) {
    geometry.setAttribute('normal', new THREE.Float32BufferAttribute(mesh.attributes.normal.array.flat(), 3))
  } else {
    geometry.computeVertexNormals()
  }
  const treated = creased ? toCreasedNormals(geometry, THREE.MathUtils.degToRad(45)) : geometry
  if (creased && treated !== geometry) geometry.dispose()
  treated.scale(0.001, 0.001, 0.001)
  treated.computeBoundingBox()
  return treated
}

function geometryForMesh(mesh, index) {
  if (index !== 83) return geometryFromOcct(mesh, { creased: creasedMeshes.has(index) })

  // The STEP microSD socket contains more triangles than the enclosure itself,
  // almost all of them internal contacts. Preserve its official outer bounds
  // and position with a lightweight socket body; the enclosure supplies the
  // actual side opening seen by the user.
  const geometry = new RoundedBoxGeometry(0.015, 0.01605, 0.00245, 2, 0.00045)
  geometry.translate(0.0631, 0.01457, -0.00628)
  geometry.computeBoundingBox()
  return geometry
}

function materialRoleForMesh(role, index) {
  if (frontGlassMeshes.has(index)) return 'FRONT_GLASS'
  if (frontTrimMeshes.has(index)) return 'FRONT_STACK_CLEAR'
  if (rearCoverMeshes.has(index)) return 'REAR_COVER'
  if (controlHousingMeshes.has(index)) return 'CONTROL_HOUSING'
  if (frontLowerCoverMeshes.has(index)) return 'FRONT_LOWER_COVER'
  if (powerSwitchMeshes.has(index)) return 'POWER_SWITCH'
  if (expansionPortMeshes.has(index)) return 'EXPANSION_PORT'
  if (microSdMeshes.has(index)) return 'MICRO_SD'
  if (ledGreenMeshes.has(index)) return 'LED_GREEN'
  if (ledRedMeshes.has(index)) return 'LED_RED'
  if (usbMetalMeshes.has(index)) return 'USB_METAL'
  if (usbDarkMeshes.has(index)) return 'USB_DARK'
  if (role === 'FASTENERS') return 'FASTENERS'
  if (role === 'MARKINGS') return 'MARKINGS'
  return role
}

function materialForRole(materialRole, materials) {
  if (materials.has(materialRole)) return materials.get(materialRole)
  let material
  if (materialRole === 'FRONT_GLASS' || materialRole === 'FRONT_STACK_CLEAR') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xf1f2ee,
      roughness: 0.3,
      metalness: 0,
      transmission: 0.01,
      transparent: true,
      opacity: materialRole === 'FRONT_GLASS' ? 0.012 : 0.006,
      depthWrite: false,
      clearcoat: 0.32,
      clearcoatRoughness: 0.28,
      name: materialRole === 'FRONT_GLASS' ? 'front-touch-glass' : 'front-stack-clear',
    })
  } else if (materialRole === 'FRONT_TRIM') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xe0e2de,
      roughness: 0.025,
      metalness: 0,
      ior: 1.55,
      specularIntensity: 1,
      clearcoat: 1,
      clearcoatRoughness: 0.012,
      name: 'front-satin-trim',
    })
  } else if (materialRole === 'WAKE_BUTTON') {
    material = new THREE.MeshPhysicalMaterial({
      color: WAKE_BUTTON_COLOR,
      roughness: 0.46,
      metalness: 0,
      ior: 1.48,
      specularIntensity: 0.48,
      clearcoat: 0.08,
      clearcoatRoughness: 0.52,
      name: 'wake-button-green',
    })
  } else if (materialRole === 'NAVIGATION_BUTTONS') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xf0f1ec,
      roughness: 0.42,
      metalness: 0,
      ior: 1.48,
      specularIntensity: 0.5,
      name: 'navigation-button-white',
    })
  } else if (materialRole === 'POWER_SWITCH') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x242724,
      roughness: 0.66,
      metalness: 0.04,
      name: 'power-switch-dark',
    })
  } else if (materialRole === 'USB_METAL' || materialRole === 'MICRO_SD' || materialRole === 'FASTENERS') {
    material = new THREE.MeshPhysicalMaterial({
      color: materialRole === 'MICRO_SD' ? 0x777d7a : 0xaeb3b1,
      roughness: materialRole === 'FASTENERS' ? 0.32 : 0.3,
      metalness: 1,
      name: materialRole === 'USB_METAL'
        ? 'usb-c-brushed-steel'
        : materialRole === 'MICRO_SD'
          ? 'microsd-socket-steel'
          : 'rear-fastener-brushed-steel',
    })
  } else if (materialRole === 'USB_DARK' || materialRole === 'EXPANSION_PORT') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x1f2321,
      roughness: 0.6,
      metalness: 0.08,
      name: materialRole === 'USB_DARK' ? 'usb-c-insert-dark' : 'expansion-port-dark',
    })
  } else if (materialRole === 'CAVITY_DARK') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x343936,
      roughness: 0.88,
      metalness: 0.04,
      name: 'enclosure-cavity-depth',
    })
  } else if (materialRole === 'LED_GREEN' || materialRole === 'LED_RED') {
    material = new THREE.MeshPhysicalMaterial({
      color: materialRole === 'LED_GREEN' ? 0x75a83c : 0xb8493e,
      roughness: 0.3,
      metalness: 0,
      clearcoat: 0.35,
      name: materialRole === 'LED_GREEN' ? 'status-led-green' : 'status-led-red',
    })
  } else if (materialRole === 'CONTROLS') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x2b2e2c,
      roughness: 0.7,
      metalness: 0.04,
      name: 'control-understructure-dark',
    })
  } else if (materialRole === 'REAR_COVER' ||
      materialRole === 'CONTROL_HOUSING' ||
      materialRole === 'FRONT_LOWER_COVER') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xdedfdc,
      roughness: 0.52,
      metalness: 0,
      clearcoat: 0.05,
      clearcoatRoughness: 0.62,
      name: materialRole === 'REAR_COVER'
        ? 'rear-service-cover'
        : materialRole === 'CONTROL_HOUSING'
          ? 'control-surround-white'
          : 'front-lower-cover',
    })
  } else if (materialRole === 'KEYHOLE_RECESS') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xd9dad6,
      roughness: 0.68,
      metalness: 0,
      name: 'keyhole-recess-white',
    })
  } else if (materialRole === 'KEYHOLE_SHADOW') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x7d827f,
      roughness: 0.9,
      metalness: 0,
      name: 'keyhole-recess-shadow',
    })
  } else if (materialRole === 'THREADED_INSERT') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x555b58,
      roughness: 0.36,
      metalness: 0.82,
      name: 'mounting-thread-gunmetal',
    })
  } else if (materialRole === 'THREAD_HIGHLIGHT') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x9aa09c,
      roughness: 0.3,
      metalness: 0.9,
      name: 'mounting-thread-highlight',
    })
  } else if (materialRole === 'MARKINGS') {
    material = new THREE.MeshBasicMaterial({
      color: 0x363a37,
      polygonOffset: true,
      polygonOffsetFactor: -2,
      polygonOffsetUnits: -2,
      name: 'rear-product-markings',
    })
  } else {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xd9dad7,
      roughness: 0.5,
      metalness: 0,
      ior: 1.52,
      specularIntensity: 0.52,
      clearcoat: 0.06,
      clearcoatRoughness: 0.58,
      name: 'enclosure-white-powder-coat',
    })
  }
  materials.set(materialRole, material)
  return material
}

function addTopControls(group, materials) {
  const caps = [
    { x: 0.05748, materialRole: 'WAKE_BUTTON', width: 0.01, depth: 0.0077 },
    { x: 0.04648, materialRole: 'NAVIGATION_BUTTONS', width: 0.009, depth: 0.0072 },
    { x: 0.037, materialRole: 'NAVIGATION_BUTTONS', width: 0.009, depth: 0.0072 },
  ]
  for (const [index, cap] of caps.entries()) {
    const object = new THREE.Mesh(
      new RoundedBoxGeometry(cap.width, 0.00165, cap.depth, 4, 0.00145),
      materialForRole(cap.materialRole, materials),
    )
    object.name = index === 0 ? 'WAKE_BUTTON_CAP' : `NAVIGATION_BUTTON_CAP_${index}`
    object.position.set(cap.x, 0.0915, -0.00885)
    object.userData = { role: 'CONTROLS', materialRole: cap.materialRole, sourceMesh: 33 }
    group.add(object)
  }
}

function addFrontBezel(group, materials) {
  const material = materialForRole('FRONT_TRIM', materials)
  const outer = { width: 0.2205, height: 0.1817, centerY: -0.00155 }
  const inner = {
    width: E1003_MODEL.screenMm.width / 1000,
    height: E1003_MODEL.screenMm.height / 1000,
    centerY: 0.00494,
  }
  const addRoundedRectangle = (path, { width, height, centerY }, radius) => {
    const left = -width / 2
    const right = width / 2
    const bottom = centerY - height / 2
    const top = centerY + height / 2
    path.moveTo(left + radius, bottom)
    path.lineTo(right - radius, bottom)
    path.quadraticCurveTo(right, bottom, right, bottom + radius)
    path.lineTo(right, top - radius)
    path.quadraticCurveTo(right, top, right - radius, top)
    path.lineTo(left + radius, top)
    path.quadraticCurveTo(left, top, left, top - radius)
    path.lineTo(left, bottom + radius)
    path.quadraticCurveTo(left, bottom, left + radius, bottom)
  }

  const shape = new THREE.Shape()
  addRoundedRectangle(shape, outer, 0.0022)
  const opening = new THREE.Path()
  addRoundedRectangle(opening, inner, 0.0018)
  shape.holes.push(opening)

  const geometry = new THREE.ExtrudeGeometry(shape, {
    depth: 0.00045,
    bevelEnabled: true,
    bevelSize: 0.00028,
    bevelThickness: 0.00016,
    bevelSegments: 2,
    curveSegments: 8,
    steps: 1,
  })
  geometry.computeBoundingBox()
  const object = new THREE.Mesh(geometry, material)
  object.name = 'FRONT_BEZEL'
  object.position.z = 0.00218
  object.userData = { role: 'BODY', materialRole: 'FRONT_TRIM' }
  group.add(object)
}

function addRearInternalBacking(group, materials) {
  const keyholeShadow = new THREE.Mesh(
    new THREE.PlaneGeometry(0.022, 0.017),
    materialForRole('KEYHOLE_SHADOW', materials),
  )
  keyholeShadow.name = 'REAR_KEYHOLE_SHADOW'
  keyholeShadow.position.set(0, 0.003, -0.01185)
  keyholeShadow.rotation.y = Math.PI
  keyholeShadow.userData = { role: 'BODY', materialRole: 'KEYHOLE_SHADOW' }
  group.add(keyholeShadow)

  const keyhole = new THREE.Mesh(
    new THREE.PlaneGeometry(0.0205, 0.0155),
    materialForRole('KEYHOLE_RECESS', materials),
  )
  keyhole.name = 'REAR_KEYHOLE_RECESS'
  keyhole.position.set(0.00045, 0.00255, -0.01215)
  keyhole.rotation.y = Math.PI
  keyhole.userData = { role: 'BODY', materialRole: 'KEYHOLE_RECESS' }
  group.add(keyhole)

  for (const [index, definition] of threadedInsertDefinitions.entries()) {
    const [xMm, yMm] = definition.centerMm
    const x = xMm / 1000
    const y = yMm / 1000
    const insert = new THREE.Mesh(
      new THREE.CircleGeometry(0.00275, 48),
      materialForRole('THREADED_INSERT', materials),
    )
    insert.name = `REAR_THREADED_INSERT_${index + 1}`
    insert.position.set(x, y, -0.01578)
    insert.userData = {
      role: 'BODY',
      materialRole: 'THREADED_INSERT',
      sourceMesh: definition.sourceMesh,
      centerSource: 'STEP bounding-box centre',
    }
    group.add(insert)

    for (const [ringIndex, radius] of [0.00218, 0.00162, 0.00106].entries()) {
      const thread = new THREE.Mesh(
        new THREE.TorusGeometry(radius, 0.000105, 5, 32),
        materialForRole('THREAD_HIGHLIGHT', materials),
      )
      thread.name = `REAR_THREAD_${index + 1}_${ringIndex + 1}`
      thread.position.set(x, y, -0.01581 - ringIndex * 0.00001)
      thread.userData = {
        role: 'BODY',
        materialRole: 'THREAD_HIGHLIGHT',
        sourceMesh: definition.sourceMesh,
      }
      group.add(thread)
    }
  }

  const vents = new THREE.Mesh(
    new THREE.PlaneGeometry(0.09, 0.012),
    materialForRole('USB_DARK', materials),
  )
  vents.name = 'REAR_VENT_DEPTH'
  vents.position.set(0, 0.024, -0.0122)
  vents.rotation.y = Math.PI
  vents.userData = { role: 'BODY', materialRole: 'VENT_RECESS' }
  group.add(vents)
}

function addTopInternalBacking(group, materials) {
  const microphoneDepth = new THREE.Mesh(
    new THREE.PlaneGeometry(0.078, 0.014),
    materialForRole('CAVITY_DARK', materials),
  )
  microphoneDepth.name = 'TOP_MICROPHONE_DEPTH'
  microphoneDepth.position.set(0.028, 0.0894, -0.007)
  microphoneDepth.rotation.x = -Math.PI / 2
  microphoneDepth.userData = { role: 'BODY', materialRole: 'TOP_CAVITY_DEPTH' }
  group.add(microphoneDepth)
}

function createScreen() {
  const geometry = new THREE.PlaneGeometry(
    E1003_MODEL.screenMm.width / 1000,
    E1003_MODEL.screenMm.height / 1000,
  )
  const material = new THREE.MeshBasicMaterial({
    color: 0xebece4,
    toneMapped: false,
    name: 'screen-preview',
  })
  const screen = new THREE.Mesh(geometry, material)
  screen.name = 'SCREEN'
  // The active area is recorded by the STEP front stack at y=4.94 mm. Keep
  // the live texture behind the physical trim and touch-glass surfaces.
  screen.position.set(0, 0.00494, 0.00022)
  screen.userData.role = 'SCREEN'
  return screen
}

function buildScene(imported) {
  const root = new THREE.Group()
  root.name = 'E1003'
  root.userData = {
    source: E1003_MODEL.sourceUrl,
    enclosureMm: E1003_MODEL.enclosureMm,
    publicationRestricted: false,
  }
  const roleGroups = new Map()
  const materials = new Map()
  for (const role of Object.keys(meshRoles)) {
    const group = new THREE.Group()
    group.name = role
    group.userData.role = role
    root.add(group)
    roleGroups.set(role, group)
  }
  imported.meshes.forEach((mesh, index) => {
    const role = roleForMesh(index)
    if (!role) return
    const materialRole = materialRoleForMesh(role, index)
    const object = new THREE.Mesh(
      geometryForMesh(mesh, index),
      materialForRole(materialRole, materials),
    )
    object.name = `${role}_${String(index).padStart(3, '0')}`
    object.userData = {
      role,
      materialRole,
      sourceMesh: index,
      ...(index === 83 ? { geometryMode: 'outer-bounds-visual-proxy' } : {}),
    }
    if (role === 'MARKINGS') {
      object.position.z = -0.00008
      object.renderOrder = 4
    }
    roleGroups.get(role).add(object)
  })
  addTopControls(roleGroups.get('CONTROLS'), materials)
  addFrontBezel(roleGroups.get('BODY'), materials)
  addRearInternalBacking(roleGroups.get('BODY'), materials)
  addTopInternalBacking(roleGroups.get('BODY'), materials)
  root.add(createScreen())
  root.position.y = 0.00155
  root.updateMatrixWorld(true)
  return root
}

function inspectScene(scene) {
  for (const role of E1003_MODEL.requiredRoles) {
    const object = scene.getObjectByName(role)
    if (!object) throw new Error(`Model is missing the ${role} scene role`)
    const bounds = new THREE.Box3().setFromObject(object)
    if (bounds.isEmpty()) throw new Error(`Model role ${role} is empty`)
  }
  const enclosureBounds = new THREE.Box3()
  for (const role of ['BODY', 'CONTROLS', 'PORTS']) {
    enclosureBounds.union(new THREE.Box3().setFromObject(scene.getObjectByName(role)))
  }
  const size = enclosureBounds.getSize(new THREE.Vector3()).multiplyScalar(1000)
  if (Math.abs(size.x - E1003_MODEL.enclosureMm.width) > 2 ||
      Math.abs(size.y - E1003_MODEL.enclosureMm.height) > 3 ||
      Math.abs(size.z - E1003_MODEL.enclosureMm.depth) > 2) {
    throw new Error(`Model enclosure is ${size.x.toFixed(1)} × ${size.y.toFixed(1)} × ${size.z.toFixed(1)} mm`)
  }
  const screenSize = new THREE.Box3().setFromObject(scene.getObjectByName('SCREEN'))
    .getSize(new THREE.Vector3())
  if (Math.abs(screenSize.x / screenSize.y - E1003_MODEL.screenAspect) > 0.001) {
    throw new Error('Model screen does not preserve the 4:3 display plane')
  }
  const retainedSourceMeshes = new Set()
  const materialNames = new Set()
  scene.traverse((object) => {
    if (!object.isMesh) return
    if (Number.isInteger(object.userData.sourceMesh)) retainedSourceMeshes.add(object.userData.sourceMesh)
    if (object.material?.name) materialNames.add(object.material.name)
  })
  for (const sourceMesh of [0, 14, 17, 25, 32, 33, 35, 36, 37, 38, 39, 41, 42, 83, 106, 107]) {
    if (!retainedSourceMeshes.has(sourceMesh)) throw new Error(`Model is missing outward STEP mesh ${sourceMesh}`)
  }
  for (const material of [
    'front-touch-glass',
    'wake-button-green',
    'navigation-button-white',
    'usb-c-brushed-steel',
    'rear-fastener-brushed-steel',
    'power-switch-dark',
    'rear-service-cover',
    'control-surround-white',
    'front-lower-cover',
  ]) {
    if (!materialNames.has(material)) throw new Error(`Model is missing physical material ${material}`)
  }
  return {
    enclosureMm: {
      width: Number(size.x.toFixed(1)),
      height: Number(size.y.toFixed(1)),
      depth: Number(size.z.toFixed(1)),
    },
  }
}

class NodeFileReader {
  readAsArrayBuffer(blob) {
    blob.arrayBuffer().then((buffer) => {
      this.result = buffer
      this.onloadend?.({ target: this })
    }, (error) => this.onerror?.(error))
  }
}

async function exportBinary(scene) {
  if (!globalThis.FileReader) globalThis.FileReader = NodeFileReader
  return new Uint8Array(await new GLTFExporter().parseAsync(scene, {
    binary: true,
    onlyVisible: true,
  }))
}

async function verifyExport(binary) {
  const buffer = binary.buffer.slice(binary.byteOffset, binary.byteOffset + binary.byteLength)
  const exported = await new GLTFLoader().parseAsync(buffer, '')
  return inspectScene(exported.scene)
}

async function main() {
  const temporaryDirectory = await mkdtemp(join(tmpdir(), 'windpeek-e1003-'))
  try {
    await mkdir(outputDirectory, { recursive: true })
    const response = await fetch(E1003_MODEL.sourceUrl, { signal: AbortSignal.timeout(30_000) })
    if (!response.ok) throw new Error(`CAD download failed with HTTP ${response.status}`)
    const sourceBytes = new Uint8Array(await response.arrayBuffer())
    const sourceSha256 = hash(sourceBytes)
    if (sourceSha256 !== E1003_MODEL.sourceSha256) {
      throw new Error(`CAD source changed unexpectedly (${sourceSha256})`)
    }
    await writeFile(join(temporaryDirectory, 'e1003.stp'), sourceBytes)
    const occt = await require('occt-import-js')()
    const imported = occt.ReadStepFile(sourceBytes, {
      linearUnit: 'millimeter',
      linearDeflectionType: 'absolute_value',
      linearDeflection: 0.25,
      angularDeflection: 0.25,
    })
    if (!imported.success) throw new Error('OpenCascade could not read the STEP assembly')

    const scene = buildScene(imported)
    const measured = inspectScene(scene)
    const binary = await exportBinary(scene)
    if (binary.byteLength > E1003_MODEL.maxBytes) {
      throw new Error(`Generated model is ${(binary.byteLength / 1024 / 1024).toFixed(2)} MB; limit is 3 MB`)
    }
    await verifyExport(binary)
    await writeFile(modelPath, binary)

    const packageJson = JSON.parse(await readFile(join(projectRoot, 'package.json'), 'utf8'))
    await writeFile(provenancePath, `${JSON.stringify({
      generatedAt: new Date().toISOString(),
      source: {
        url: E1003_MODEL.sourceUrl,
        sha256: sourceSha256,
        documentedDimensionsMm: E1003_MODEL.enclosureMm,
      },
      conversion: {
        importer: `occt-import-js@${packageJson.devDependencies['occt-import-js']}`,
        exporter: `three@${packageJson.dependencies.three}`,
        retainedSourceMeshes: Object.fromEntries(
          Object.entries(meshRoles).map(([role, indexes]) => [role, [...indexes]]),
        ),
        visualProxies: {
          83: 'MicroSD socket outer bounds; internal electrical contacts omitted for browser performance.',
        },
        addedScreenMm: E1003_MODEL.screenMm,
      },
      output: {
        file: 'e1003.glb',
        bytes: binary.byteLength,
        sha256: hash(binary),
        roles: E1003_MODEL.requiredRoles,
        sourceMeshes: imported.meshes.length,
        measured,
      },
      publication: {
        redistributionConfirmed: true,
        permissionBasis: 'Project owner confirmed direct permission from Seeed Studio on 2026-08-26.',
      },
    }, null, 2)}\n`)
    console.log(`Prepared ${modelPath} (${(binary.byteLength / 1024 / 1024).toFixed(2)} MB)`)
  } finally {
    await rm(temporaryDirectory, { recursive: true, force: true })
  }
}

main().catch((error) => {
  console.error(error.message)
  process.exitCode = 1
})
