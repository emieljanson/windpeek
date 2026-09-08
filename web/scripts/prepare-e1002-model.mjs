import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { createRequire } from 'node:module'
import * as THREE from 'three'
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js'
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js'
import { toCreasedNormals } from 'three/examples/jsm/utils/BufferGeometryUtils.js'
import { E1002_MODEL } from '../src/assets/e1002.js'
import { E1003_MODEL } from '../src/assets/e1003.js'
import { WAKE_BUTTON_COLOR } from '../src/configurator/productColors.js'

const require = createRequire(import.meta.url)
const projectRoot = dirname(dirname(fileURLToPath(import.meta.url)))
const outputDirectory = join(projectRoot, 'public', 'devices', 'e1002')
const modelPath = join(outputDirectory, 'e1002.glb')
const provenancePath = join(outputDirectory, 'provenance.json')

const meshRoles = Object.freeze({
  BODY: new Set([0, 1, 2, 3, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 25]),
  CONTROLS: new Set([4, 5, 6, 7, 8, 9, 10, 11, 28, 29, 30, 39, 41, 50, 51, 52, 53, 54, 55]),
  PORTS: new Set([31, 32, 33, 34, 35, 37, 38, 40, 43, 44, 45, 49, 57, 59]),
  STAND: new Set([58]),
})

// The shared E1001/E1002 CAD contains two overlapping display stacks. Meshes
// 14–17 belong to the monochrome E1001 and caused z-fighting when rendered on
// top of the E1002 front. Keep the E1002 stack (18–21) only.
const excludedVariantMeshes = new Set([14, 15, 16, 17])
const excludedInternalMeshes = new Set([23, 24, 26, 27, 36, 42, 46, 47, 48, 56])
const excludedMeshes = new Set([...excludedVariantMeshes, ...excludedInternalMeshes])
const frontPanelMeshes = new Set([18, 19, 20, 21])
const displayBedMeshes = new Set([25])
const wakeButtonMeshes = new Set([13, 28, 55])
const navigationButtonMeshes = new Set([12, 29, 30, 53, 54])
const rearScrewMeshes = new Set([39, 50, 51, 52])
const e1003MarkingGroups = Object.freeze({
  TOP_CONTROLS: Object.freeze({
    meshes: new Set([241, 243, 244, 245]),
    // Green-button centres provide the common datum; all glyph spacing stays
    // exactly as drawn in the E1003 production CAD.
    translationMm: Object.freeze([-27.754, 29.85, 2.62]),
    anchorSourceMesh: 28,
  }),
  MICRO_SD: Object.freeze({
    meshes: new Set(Array.from({ length: 6 }, (_, offset) => 246 + offset)),
    translationMm: Object.freeze([-28.163, 78.347, 2.62]),
    anchorSourceMesh: 33,
  }),
  POWER_SWITCH: Object.freeze({
    meshes: new Set(Array.from({ length: 7 }, (_, offset) => 252 + offset)),
    translationMm: Object.freeze([-28.163, 78.447, 2.62]),
    anchorSourceMesh: 32,
  }),
  USB_C: Object.freeze({
    meshes: new Set(Array.from({ length: 5 }, (_, offset) => 259 + offset)),
    translationMm: Object.freeze([-28.163, 80.347, 2.62]),
    anchorSourceMesh: 37,
  }),
  STATUS_CIRCLE: Object.freeze({
    meshes: new Set([264]),
    translationMm: Object.freeze([-28.163, 80.747, 2.62]),
    anchorSourceMesh: 32,
  }),
  LIGHTNING_BOLT: Object.freeze({
    meshes: new Set([265]),
    translationMm: Object.freeze([-28.163, 80.247, 2.62]),
    anchorSourceMesh: 37,
  }),
  EXPANSION_PORT: Object.freeze({
    meshes: new Set(Array.from({ length: 7 }, (_, offset) => 266 + offset)),
    // Exact centre-to-centre offset between both CAD expansion connectors.
    translationMm: Object.freeze([29, 27.59, 2.62]),
    anchorSourceMesh: 31,
  }),
})

function hash(content) {
  return createHash('sha256').update(content).digest('hex')
}

function roleForMesh(index) {
  for (const [role, indexes] of Object.entries(meshRoles)) {
    if (indexes.has(index)) return role
  }
  return null
}

function materialRoleForMesh(role, index) {
  if (rearScrewMeshes.has(index)) return 'REAR_SCREWS'
  if (wakeButtonMeshes.has(index)) return 'WAKE_BUTTON'
  if (navigationButtonMeshes.has(index)) return 'NAVIGATION_BUTTONS'
  if (role !== 'BODY') return role
  if (frontPanelMeshes.has(index)) return 'FRONT_PANEL'
  if (displayBedMeshes.has(index)) return 'DISPLAY_BED'
  return role
}

function lockPlanarFrontNormals(geometry) {
  const position = geometry.attributes.position
  const normal = geometry.attributes.normal
  const frontZ = geometry.boundingBox.max.z
  const tolerance = 0.000001

  // toCreasedNormals returns non-indexed triangles. Keep the visible face of
  // the single-piece plastic ring perfectly planar, rather than averaging its
  // normals with the rounded outer and display-opening edges. That averaging
  // created diagonal highlight wedges in the corners that looked like folds.
  for (let index = 0; index < position.count; index += 3) {
    const isFrontFace = [index, index + 1, index + 2]
      .every((vertex) => Math.abs(position.getZ(vertex) - frontZ) <= tolerance)
    if (!isFrontFace) continue
    for (let vertex = index; vertex < index + 3; vertex += 1) normal.setXYZ(vertex, 0, 0, 1)
  }
  normal.needsUpdate = true
  return geometry
}

function geometryFromOcct(mesh, { planarFront = false } = {}) {
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(mesh.attributes.position.array.flat(), 3))
  geometry.setIndex(mesh.index.array.flat())
  // Rebuild CAD normals before scaling to metres. At millimetre scale the
  // helper can reliably match coincident vertices, smooth the real STEP
  // fillets and retain intentional sharp seams above the crease angle.
  const treated = toCreasedNormals(geometry, THREE.MathUtils.degToRad(45))
  if (treated !== geometry) geometry.dispose()
  treated.scale(0.001, 0.001, 0.001)
  treated.computeBoundingBox()
  if (planarFront) lockPlanarFrontNormals(treated)
  return treated
}

function materialForMesh(role, index, materials) {
  const materialRole = materialRoleForMesh(role, index)
  if (materials.has(materialRole)) return materials.get(materialRole)
  let material
  if (materialRole === 'MARKINGS') {
    material = new THREE.MeshBasicMaterial({
      color: 0x363a37,
      polygonOffset: true,
      polygonOffsetFactor: -2,
      polygonOffsetUnits: -2,
      name: 'rear-product-markings',
    })
  } else if (materialRole === 'PORTS') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x252826,
      roughness: 0.54,
      metalness: 0.08,
      name: 'port-dark',
    })
  } else if (materialRole === 'CONTROLS') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xd9dad4,
      roughness: 0.66,
      metalness: 0,
      ior: 1.48,
      name: 'control-matte-plastic',
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
  } else if (materialRole === 'REAR_SCREWS') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xa8adb0,
      roughness: 0.32,
      metalness: 1,
      name: 'rear-fastener-brushed-steel',
    })
  } else if (materialRole === 'STAND') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0x777b77,
      roughness: 0.78,
      metalness: 0,
      ior: 1.46,
      name: 'stand-printed-polymer',
    })
  } else if (materialRole === 'FRONT_PANEL') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xe7e8e4,
      roughness: 0.2,
      metalness: 0,
      ior: 1.47,
      specularIntensity: 0.7,
      clearcoat: 0.42,
      clearcoatRoughness: 0.24,
      name: 'front-satin-plastic',
    })
  } else if (materialRole === 'DISPLAY_BED') {
    material = new THREE.MeshPhysicalMaterial({
      color: 0xdedfdc,
      roughness: 0.4,
      metalness: 0,
      ior: 1.46,
      specularIntensity: 0.44,
      name: 'display-recess-trim',
    })
  } else {
    // Powder coat behaves optically like a dielectric paint layer rather than
    // bare metal, so metalness stays at zero. The web viewer adds the very
    // fine orange-peel relief at runtime where it can be scaled consistently.
    material = new THREE.MeshPhysicalMaterial({
      color: 0xd9dad7,
      roughness: 0.48,
      metalness: 0,
      ior: 1.52,
      specularIntensity: 0.52,
      clearcoat: 0.08,
      clearcoatRoughness: 0.56,
      name: 'enclosure-white-powder-coat',
    })
  }
  materials.set(materialRole, material)
  return material
}

function createScreen() {
  // The display opening is vertically asymmetric: the lower bezel is deeper
  // than the upper bezel, so the visible panel sits 6 mm above enclosure center.
  const displayCenterOffsetY = 0.006
  const geometry = new THREE.PlaneGeometry(0.159, 0.0954)
  const material = new THREE.MeshBasicMaterial({ color: 0xebece4, toneMapped: false, name: 'screen-preview' })
  const screen = new THREE.Mesh(geometry, material)
  screen.name = 'SCREEN'
  // The e-paper panel sits behind the front trim instead of on top of it.
  screen.position.set(0, 0.06 + displayCenterOffsetY, 0.00278)
  screen.userData.role = 'SCREEN'
  return screen
}

function addE1003Markings(root, imported, materials) {
  const group = new THREE.Group()
  group.name = 'MARKINGS'
  group.userData.role = 'MARKINGS'
  const material = materialForMesh('MARKINGS', 0, materials)

  for (const [groupName, definition] of Object.entries(e1003MarkingGroups)) {
    for (const sourceMesh of definition.meshes) {
      const mesh = imported.meshes[sourceMesh]
      if (!mesh) throw new Error(`E1003 marking source mesh ${sourceMesh} is missing`)
      const object = new THREE.Mesh(geometryFromOcct(mesh), material)
      object.name = `MARKINGS_${groupName}_${sourceMesh}`
      object.position.fromArray(definition.translationMm).multiplyScalar(0.001)
      object.renderOrder = 4
      object.userData = {
        role: 'MARKINGS',
        materialRole: 'MARKINGS',
        sourceDevice: 'E1003',
        sourceMesh,
        markingGroup: groupName,
        anchorSourceMesh: definition.anchorSourceMesh,
      }
      group.add(object)
    }
  }
  root.add(group)
}

function buildScene(imported, e1003Imported) {
  const root = new THREE.Group()
  root.name = 'E1002'
  root.userData = {
    source: E1002_MODEL.sourceUrl,
    enclosureMm: E1002_MODEL.enclosureMm,
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
    if (excludedMeshes.has(index)) return
    const role = roleForMesh(index)
    if (!role) return
    const materialRole = materialRoleForMesh(role, index)
    const object = new THREE.Mesh(
      geometryFromOcct(mesh, { planarFront: frontPanelMeshes.has(index) }),
      materialForMesh(role, index, materials),
    )
    object.name = `${role}_${String(index).padStart(2, '0')}`
    object.userData = { role, materialRole, sourceMesh: index }
    roleGroups.get(role).add(object)
  })

  addE1003Markings(root, e1003Imported, materials)
  root.add(createScreen())
  root.position.y = -0.06
  root.updateMatrixWorld(true)
  return root
}

function inspectScene(scene) {
  const fullBox = new THREE.Box3().setFromObject(scene)
  const enclosureBox = new THREE.Box3()
  for (const role of ['BODY', 'CONTROLS', 'PORTS']) {
    enclosureBox.union(new THREE.Box3().setFromObject(scene.getObjectByName(role)))
  }
  const enclosureSize = enclosureBox.getSize(new THREE.Vector3()).multiplyScalar(1000)
  const fullSize = fullBox.getSize(new THREE.Vector3()).multiplyScalar(1000)
  const measured = {
    enclosureMm: {
      width: Number(enclosureSize.x.toFixed(1)),
      height: Number(enclosureSize.y.toFixed(1)),
      depth: Number(enclosureSize.z.toFixed(1)),
    },
    totalStandDepthMm: Number(fullSize.z.toFixed(1)),
  }

  const tolerances = { width: 2, height: 1, depth: 1, standDepth: 1 }
  if (Math.abs(measured.enclosureMm.width - E1002_MODEL.enclosureMm.width) > tolerances.width) {
    throw new Error(`Model width ${measured.enclosureMm.width} mm does not match the documented enclosure`)
  }
  if (Math.abs(measured.enclosureMm.height - E1002_MODEL.enclosureMm.height) > tolerances.height) {
    throw new Error(`Model height ${measured.enclosureMm.height} mm does not match the documented enclosure`)
  }
  if (Math.abs(measured.enclosureMm.depth - E1002_MODEL.enclosureMm.depth) > tolerances.depth) {
    throw new Error(`Model depth ${measured.enclosureMm.depth} mm does not match the documented enclosure`)
  }
  if (Math.abs(measured.totalStandDepthMm - E1002_MODEL.enclosureMm.standDepth) > tolerances.standDepth) {
    throw new Error(`Model stand depth ${measured.totalStandDepthMm} mm does not match the documented assembly`)
  }
  for (const role of E1002_MODEL.requiredRoles) {
    const roleObject = scene.getObjectByName(role)
    if (!roleObject) throw new Error(`Model is missing the ${role} scene role`)
    let meshCount = 0
    roleObject.traverse((child) => { if (child.isMesh) meshCount += 1 })
    if (!meshCount) throw new Error(`Model role ${role} contains no renderable mesh`)
    const bounds = new THREE.Box3().setFromObject(roleObject)
    if (bounds.isEmpty() || !bounds.min.toArray().every(Number.isFinite) || !bounds.max.toArray().every(Number.isFinite)) {
      throw new Error(`Model role ${role} has invalid bounds`)
    }
  }
  const screenSize = new THREE.Box3().setFromObject(scene.getObjectByName('SCREEN')).getSize(new THREE.Vector3())
  if (Math.abs(screenSize.x / screenSize.y - E1002_MODEL.screenAspect) > 0.01 || screenSize.z > 0.0001) {
    throw new Error('Model screen does not preserve the outward 800:480 plane')
  }
  return measured
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
  const exporter = new GLTFExporter()
  return new Uint8Array(await exporter.parseAsync(scene, {
    binary: true,
    onlyVisible: true,
    trs: false,
  }))
}

async function verifyExport(binary) {
  const buffer = binary.buffer.slice(binary.byteOffset, binary.byteOffset + binary.byteLength)
  const exported = await new GLTFLoader().parseAsync(buffer, '')
  return inspectScene(exported.scene)
}

async function main() {
  const temporaryDirectory = await mkdtemp(join(tmpdir(), 'windpeek-e1002-'))
  try {
    await mkdir(outputDirectory, { recursive: true })
    const sourcePath = join(temporaryDirectory, 'e1002.stp')
    const response = await fetch(E1002_MODEL.sourceUrl, { signal: AbortSignal.timeout(30_000) })
    if (!response.ok) throw new Error(`CAD download failed with HTTP ${response.status}`)
    const sourceBytes = new Uint8Array(await response.arrayBuffer())
    const sourceSha256 = hash(sourceBytes)
    if (sourceSha256 !== E1002_MODEL.sourceSha256) {
      throw new Error(`CAD source changed unexpectedly (${sourceSha256}); review its mesh mapping before converting`)
    }
    await writeFile(sourcePath, sourceBytes)

    const e1003Response = await fetch(E1003_MODEL.sourceUrl, { signal: AbortSignal.timeout(30_000) })
    if (!e1003Response.ok) throw new Error(`E1003 marking CAD download failed with HTTP ${e1003Response.status}`)
    const e1003SourceBytes = new Uint8Array(await e1003Response.arrayBuffer())
    const e1003SourceSha256 = hash(e1003SourceBytes)
    if (e1003SourceSha256 !== E1003_MODEL.sourceSha256) {
      throw new Error(`E1003 marking CAD source changed unexpectedly (${e1003SourceSha256})`)
    }

    const occt = await require('occt-import-js')()
    const imported = occt.ReadStepFile(sourceBytes, {
      linearUnit: 'millimeter',
      linearDeflectionType: 'absolute_value',
      linearDeflection: 0.2,
      angularDeflection: 0.25,
    })
    if (!imported.success) throw new Error('OpenCascade could not read the STEP assembly')
    const e1003Imported = occt.ReadStepFile(e1003SourceBytes, {
      linearUnit: 'millimeter',
      linearDeflectionType: 'absolute_value',
      linearDeflection: 0.2,
      angularDeflection: 0.25,
    })
    if (!e1003Imported.success) throw new Error('OpenCascade could not read the E1003 marking geometry')

    const scene = buildScene(imported, e1003Imported)
    const measured = inspectScene(scene)
    const binary = await exportBinary(scene)
    if (binary.byteLength > E1002_MODEL.maxBytes) {
      throw new Error(`Generated model is ${(binary.byteLength / 1024 / 1024).toFixed(2)} MB; limit is 3 MB`)
    }
    await verifyExport(binary)

    await writeFile(modelPath, binary)
    const packageJson = JSON.parse(await readFile(join(projectRoot, 'package.json'), 'utf8'))
    const provenance = {
      generatedAt: new Date().toISOString(),
      source: {
        url: E1002_MODEL.sourceUrl,
        sha256: sourceSha256,
        documentedDimensionsMm: E1002_MODEL.enclosureMm,
      },
      conversion: {
        importer: `occt-import-js@${packageJson.devDependencies['occt-import-js']}`,
        exporter: `three@${packageJson.dependencies.three}`,
        linearDeflectionMm: 0.2,
        angularDeflection: 0.25,
        normalTreatment: '45-degree crease-aware normals with a planar front-face lock',
        excludedInternalMeshes: [...excludedInternalMeshes],
        excludedVariantMeshes: [...excludedVariantMeshes],
        markings: {
          sourceDevice: 'E1003',
          sourceSha256: e1003SourceSha256,
          groups: Object.fromEntries(Object.entries(e1003MarkingGroups).map(([name, definition]) => [name, {
            meshes: [...definition.meshes],
            translationMm: definition.translationMm,
          }])),
        },
        addedScreenMm: { width: 159, height: 95.4, aspect: E1002_MODEL.screenAspect },
      },
      output: {
        file: 'e1002.glb',
        bytes: binary.byteLength,
        sha256: hash(binary),
        roles: E1002_MODEL.requiredRoles,
        sourceMeshes: imported.meshes.length,
        measured,
      },
      publication: {
        redistributionConfirmed: true,
        permissionBasis: 'Project owner confirmed direct permission from Seeed Studio on 2026-08-26.',
      },
    }
    await writeFile(provenancePath, `${JSON.stringify(provenance, null, 2)}\n`)
    console.log(`Prepared ${modelPath}`)
    console.log(`${(binary.byteLength / 1024 / 1024).toFixed(2)} MB · ${imported.meshes.length} source meshes · roles ${E1002_MODEL.requiredRoles.join(', ')}`)
  } finally {
    await rm(temporaryDirectory, { recursive: true, force: true })
  }
}

main().catch((error) => {
  console.error(error.message)
  process.exitCode = 1
})
