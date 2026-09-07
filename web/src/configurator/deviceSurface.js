import { filterDetailTexture } from './detailTexture'
import * as THREE from 'three'
import { BOARD_IDS } from '../config/configuration'

const PRIMARY_SOFTBOX = Object.freeze({
  direction: Object.freeze([0.5, -0.22, 0.84]),
  halfWidth: 0.055,
  halfHeight: 0.15,
})

const SECONDARY_SOFTBOX = Object.freeze({
  direction: Object.freeze([-0.48, -0.18, 0.86]),
  halfWidth: 0.042,
  halfHeight: 0.12,
})

const POWDER_COAT_MATERIALS = new Set([
  'enclosure-white-powder-coat',
  'rear-service-cover',
  'control-surround-white',
  'front-lower-cover',
])

// BODY_18/BODY_19 expose a 160.2 × 94.9 mm opening. This scale keeps the
// canonical 159 × 95.4 mm (800 × 480) UI undistorted while placing exactly
// 1.525 mm of screen underneath each of the four visible bezel edges.
export const SCREEN_BEZEL_OVERSCAN = 1.02672956

function createE100xProductLabelTexture(boardId) {
  const model = boardId === BOARD_IDS.E1001 ? 'E1001' : 'E1002'
  const canvas = document.createElement('canvas')
  canvas.width = 1040
  canvas.height = 640
  const context = canvas.getContext('2d')
  context.fillStyle = '#f1f2ef'
  context.fillRect(0, 0, canvas.width, canvas.height)
  context.strokeStyle = '#a7aca9'
  context.lineWidth = 4
  context.strokeRect(2, 2, canvas.width - 4, canvas.height - 4)
  context.fillStyle = '#242825'
  context.textAlign = 'left'
  context.font = '700 70px Arial, sans-serif'
  context.fillText('seeed studio', 48, 92)
  context.font = '600 31px Arial, sans-serif'
  context.fillText(`reTerminal ${model} ePaper Display`, 50, 138)
  context.font = '400 25px Arial, sans-serif'
  context.fillText(`Model: ${model}`, 50, 188)
  context.fillText('FCC ID: Z4T-E100X', 50, 226)
  context.fillText('SKU: 100017057', 50, 264)

  const barcode = (x, y, width, height, seed) => {
    let cursor = x
    let value = seed
    while (cursor < x + width) {
      value = (value * 9301 + 49297) % 233280
      const barWidth = 2 + (value % 6)
      context.fillRect(cursor, y, barWidth, height)
      cursor += barWidth + 3 + (value % 4)
    }
  }
  barcode(50, 304, 425, 78, boardId === BOARD_IDS.E1001 ? 1001 : 1002)
  context.font = '400 20px Arial, sans-serif'
  context.fillText('S/N: 10001705725250001', 50, 412)
  barcode(50, 446, 425, 66, 72525)

  context.font = '600 88px Arial, sans-serif'
  context.fillText('CE', 625, 350)
  context.lineWidth = 7
  context.strokeStyle = '#242825'
  context.strokeRect(834, 266, 70, 105)
  context.beginPath()
  context.moveTo(820, 250)
  context.lineTo(918, 390)
  context.moveTo(918, 250)
  context.lineTo(820, 390)
  context.stroke()
  context.font = '500 24px Arial, sans-serif'
  context.fillText('R 217-252557', 620, 448)

  const texture = new THREE.CanvasTexture(canvas)
  texture.colorSpace = THREE.SRGBColorSpace
  texture.name = `${model.toLowerCase()}-product-label-texture`
  return texture
}

export function fitScreenUnderBezel(screen, overscan = SCREEN_BEZEL_OVERSCAN) {
  screen.scale.x *= overscan
  screen.scale.y *= overscan
  return screen
}

export function addDeviceRearMarkings(model, boardId, requestRender = () => {}) {
  if (![BOARD_IDS.E1001, BOARD_IDS.E1002].includes(boardId)) return () => {}

  const depthMaterial = new THREE.MeshPhysicalMaterial({
    color: 0x343936,
    roughness: 0.88,
    metalness: 0.04,
    name: 'e1002-rear-cavity-depth',
  })
  const depth = new THREE.Mesh(new THREE.PlaneGeometry(0.087, 0.113), depthMaterial)
  depth.name = 'REAR_CAVITY_DEPTH'
  depth.position.set(0, -0.0001, -0.0117)
  depth.rotation.y = Math.PI
  depth.userData = { role: 'BODY', materialRole: 'REAR_CAVITY_DEPTH' }
  model.add(depth)

  const topDepth = new THREE.Mesh(new THREE.PlaneGeometry(0.065, 0.014), depthMaterial)
  topDepth.name = 'TOP_MICROPHONE_DEPTH'
  topDepth.position.set(0.008, 0.0573, -0.006)
  topDepth.rotation.x = -Math.PI / 2
  topDepth.userData = { role: 'BODY', materialRole: 'TOP_CAVITY_DEPTH' }
  model.add(topDepth)

  const keyholeMaterial = new THREE.MeshPhysicalMaterial({
    color: 0xc7cac5,
    roughness: 0.78,
    metalness: 0,
    name: 'e1002-keyhole-recess-white',
  })
  const keyhole = new THREE.Mesh(new THREE.PlaneGeometry(0.018, 0.033), keyholeMaterial)
  keyhole.name = 'E1002_KEYHOLE_RECESS'
  keyhole.position.set(0, 0.028, -0.01175)
  keyhole.rotation.y = Math.PI
  keyhole.renderOrder = 2
  keyhole.userData = { role: 'BODY', materialRole: 'KEYHOLE_RECESS' }
  model.add(keyhole)

  const labelTexture = createE100xProductLabelTexture(boardId)
  const labelMaterial = new THREE.MeshPhysicalMaterial({
    map: labelTexture,
    color: 0xf1f2ef,
    roughness: 0.82,
    metalness: 0,
    side: THREE.DoubleSide,
    polygonOffset: true,
    polygonOffsetFactor: -2,
    polygonOffsetUnits: -2,
    name: 'e1002-product-label-paper',
  })
  const label = new THREE.Mesh(new THREE.PlaneGeometry(0.052, 0.032), labelMaterial)
  label.name = 'REAR_PRODUCT_LABEL'
  label.position.set(-0.01, -0.012, -0.01318)
  label.rotation.y = Math.PI
  label.renderOrder = 5
  model.add(label)
  requestRender()

  return () => {
    depth.removeFromParent()
    depth.geometry.dispose()
    topDepth.removeFromParent()
    topDepth.geometry.dispose()
    depthMaterial.dispose()
    keyhole.removeFromParent()
    keyhole.geometry.dispose()
    keyholeMaterial.dispose()
    label.removeFromParent()
    label.geometry.dispose()
    labelMaterial.dispose()
    labelTexture.dispose()
  }
}

function noise(x, y) {
  const value = Math.sin(x * 127.1 + y * 311.7) * 43758.5453123
  return value - Math.floor(value)
}

export function createPowderCoatNormalMap(size = 64) {
  const heights = new Float32Array(size * size)
  const sample = (x, y) => heights[((y + size) % size) * size + ((x + size) % size)]

  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const fine = noise(x, y)
      const broad = noise(Math.floor(x / 3) + 41, Math.floor(y / 3) + 73)
      heights[y * size + x] = fine * 0.58 + broad * 0.42
    }
  }

  const pixels = new Uint8Array(size * size * 4)
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const dx = (sample(x + 1, y) - sample(x - 1, y)) * 0.32
      const dy = (sample(x, y + 1) - sample(x, y - 1)) * 0.32
      const normal = new THREE.Vector3(-dx, -dy, 1).normalize()
      const offset = (y * size + x) * 4
      pixels[offset] = Math.round((normal.x * 0.5 + 0.5) * 255)
      pixels[offset + 1] = Math.round((normal.y * 0.5 + 0.5) * 255)
      pixels[offset + 2] = Math.round((normal.z * 0.5 + 0.5) * 255)
      pixels[offset + 3] = 255
    }
  }

  const texture = new THREE.DataTexture(pixels, size, size, THREE.RGBAFormat)
  texture.name = 'powder-coat-micro-normal'
  texture.wrapS = THREE.RepeatWrapping
  texture.wrapT = THREE.RepeatWrapping
  texture.colorSpace = THREE.NoColorSpace
  return filterDetailTexture(texture)
}

export function createPowderCoatRoughnessMap(size = 64) {
  const pixels = new Uint8Array(size * size * 4)
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const fine = noise(x + 19, y + 37)
      const broad = noise(Math.floor(x / 4) + 83, Math.floor(y / 4) + 29)
      const roughness = Math.round(198 + (fine * 0.62 + broad * 0.38) * 57)
      const offset = (y * size + x) * 4
      pixels[offset] = roughness
      pixels[offset + 1] = roughness
      pixels[offset + 2] = roughness
      pixels[offset + 3] = 255
    }
  }

  const texture = new THREE.DataTexture(pixels, size, size, THREE.RGBAFormat)
  texture.name = 'powder-coat-micro-roughness'
  texture.wrapS = THREE.RepeatWrapping
  texture.wrapT = THREE.RepeatWrapping
  texture.colorSpace = THREE.NoColorSpace
  return filterDetailTexture(texture)
}

export function addSurfaceProjectionUvs(geometry, repeatsPerMeter = 82) {
  if (!geometry?.attributes?.position || !geometry.attributes.normal || geometry.attributes.uv) return

  const position = geometry.attributes.position
  const normal = geometry.attributes.normal
  const uv = new Float32Array(position.count * 2)
  for (let index = 0; index < position.count; index += 1) {
    const x = position.getX(index)
    const y = position.getY(index)
    const z = position.getZ(index)
    const nx = Math.abs(normal.getX(index))
    const ny = Math.abs(normal.getY(index))
    const nz = Math.abs(normal.getZ(index))

    if (nx >= ny && nx >= nz) {
      uv[index * 2] = z * repeatsPerMeter
      uv[index * 2 + 1] = y * repeatsPerMeter
    } else if (ny >= nx && ny >= nz) {
      uv[index * 2] = x * repeatsPerMeter
      uv[index * 2 + 1] = z * repeatsPerMeter
    } else {
      uv[index * 2] = x * repeatsPerMeter
      uv[index * 2 + 1] = y * repeatsPerMeter
    }
  }
  geometry.setAttribute('uv', new THREE.BufferAttribute(uv, 2))
}

export function enhanceDeviceSurface(model, renderer) {
  const powderCoatNormal = createPowderCoatNormalMap()
  const powderCoatRoughness = createPowderCoatRoughnessMap()
  const panelReflections = []
  powderCoatNormal.anisotropy = Math.min(renderer.capabilities.getMaxAnisotropy(), 4)
  powderCoatRoughness.anisotropy = Math.min(renderer.capabilities.getMaxAnisotropy(), 4)

  model.traverse((child) => {
    if (!child.isMesh || !child.material) return
    const materials = Array.isArray(child.material) ? child.material : [child.material]
    for (const material of materials) {
      if (POWDER_COAT_MATERIALS.has(material.name)) {
        addSurfaceProjectionUvs(child.geometry)
        material.color.setHex(0xe5e5e1)
        material.normalMap = powderCoatNormal
        material.normalScale.set(0.3, 0.3)
        material.roughness = 0.78
        material.roughnessMap = powderCoatRoughness
        material.metalness = 0
        material.ior = 1.52
        material.specularIntensity = 0.65
        material.clearcoat = 0
        material.clearcoatRoughness = 0.56
        material.envMapIntensity = 0.85
        material.needsUpdate = true
      } else if (material.name === 'front-satin-plastic' || material.name === 'front-satin-trim') {
        material.color.setHex(0xe6e7e3)
        material.roughness = 0.12
        material.ior = 1.55
        material.specularIntensity = 1
        material.clearcoat = 0.65
        material.clearcoatRoughness = 0.055
        material.envMapIntensity = 1.35
        material.needsUpdate = true
        if (!child.getObjectByName('FRONT_PANEL_REFLECTION')) {
          panelReflections.push(createFrontPanelReflection(child))
        }
      } else if (material.name === 'display-recess-trim') {
        material.envMapIntensity = 0.5
      } else if (material.name === 'stand-printed-polymer') {
        material.envMapIntensity = 0.36
      } else if (material.name === 'control-matte-plastic') {
        material.envMapIntensity = 0.5
      } else if (material.name === 'wake-button-green' || material.name === 'navigation-button-white') {
        material.envMapIntensity = 0.72
      } else if (material.name === 'port-dark') {
        material.envMapIntensity = 0.68
      }
    }
  })

  return () => {
    for (const reflection of panelReflections) {
      reflection.removeFromParent()
      reflection.material.dispose()
    }
    powderCoatNormal.dispose()
    powderCoatRoughness.dispose()
  }
}

export function createFrontPanelReflection(panel) {
  const material = new THREE.ShaderMaterial({
    name: 'front-panel-softbox-reflection',
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    polygonOffset: true,
    polygonOffsetFactor: -1,
    polygonOffsetUnits: -1,
    uniforms: {
      softboxDirection: { value: new THREE.Vector3(...PRIMARY_SOFTBOX.direction).normalize() },
      softboxStrength: { value: 0.52 },
      softboxHalfWidth: { value: PRIMARY_SOFTBOX.halfWidth },
      softboxHalfHeight: { value: PRIMARY_SOFTBOX.halfHeight },
      softboxFeather: { value: 0.02 },
      secondarySoftboxDirection: {
        value: new THREE.Vector3(...SECONDARY_SOFTBOX.direction).normalize(),
      },
      secondarySoftboxStrength: { value: 0.42 },
      secondarySoftboxHalfWidth: { value: SECONDARY_SOFTBOX.halfWidth },
      secondarySoftboxHalfHeight: { value: SECONDARY_SOFTBOX.halfHeight },
      secondarySoftboxFeather: { value: 0.018 },
    },
    vertexShader: `
      varying vec3 vWorldPosition;
      varying vec3 vWorldNormal;
      void main() {
        vec4 worldPosition = modelMatrix * vec4(position, 1.0);
        vWorldPosition = worldPosition.xyz;
        vWorldNormal = normalize(mat3(modelMatrix) * normal);
        gl_Position = projectionMatrix * viewMatrix * worldPosition;
      }
    `,
    fragmentShader: `
      varying vec3 vWorldPosition;
      varying vec3 vWorldNormal;
      uniform vec3 softboxDirection;
      uniform float softboxStrength;
      uniform float softboxHalfWidth;
      uniform float softboxHalfHeight;
      uniform float softboxFeather;
      uniform vec3 secondarySoftboxDirection;
      uniform float secondarySoftboxStrength;
      uniform float secondarySoftboxHalfWidth;
      uniform float secondarySoftboxHalfHeight;
      uniform float secondarySoftboxFeather;

      float softboxMask(
        vec3 reflectedView,
        vec3 direction,
        float halfWidth,
        float halfHeight,
        float feather
      ) {
        vec3 lightDirection = normalize(direction);
        vec3 lightRight = normalize(cross(vec3(0.0, 1.0, 0.0), lightDirection));
        vec3 lightUp = normalize(cross(lightDirection, lightRight));
        vec3 reflectedOffset = reflectedView - lightDirection;
        float horizontalOffset = abs(dot(reflectedOffset, lightRight));
        float verticalOffset = abs(dot(reflectedOffset, lightUp));
        float horizontalMask = 1.0 - smoothstep(halfWidth, halfWidth + feather, horizontalOffset);
        float verticalMask = 1.0 - smoothstep(halfHeight, halfHeight + feather, verticalOffset);
        return horizontalMask * verticalMask;
      }

      void main() {
        vec3 normal = normalize(vWorldNormal);
        vec3 viewDirection = normalize(cameraPosition - vWorldPosition);
        vec3 reflectedView = reflect(-viewDirection, normal);
        float softbox = softboxMask(
          reflectedView,
          softboxDirection,
          softboxHalfWidth,
          softboxHalfHeight,
          softboxFeather
        );
        float secondarySoftbox = softboxMask(
          reflectedView,
          secondarySoftboxDirection,
          secondarySoftboxHalfWidth,
          secondarySoftboxHalfHeight,
          secondarySoftboxFeather
        );
        float reflectionAlpha = min(
          softbox * softboxStrength + secondarySoftbox * secondarySoftboxStrength,
          0.72
        );
        gl_FragColor = vec4(vec3(1.0), reflectionAlpha);
      }
    `,
  })
  const reflection = new THREE.Mesh(panel.geometry, material)
  reflection.name = 'FRONT_PANEL_REFLECTION'
  reflection.renderOrder = 1
  panel.add(reflection)
  return reflection
}

export function createScreenRecessShadow(screen) {
  const material = new THREE.ShaderMaterial({
    name: 'screen-recess-shadow',
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    vertexShader: `
      varying vec2 vUv;
      void main() {
        vUv = uv;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: `
      varying vec2 vUv;
      void main() {
        float left = 1.0 - smoothstep(0.0, 0.012, vUv.x);
        float right = 1.0 - smoothstep(0.0, 0.009, 1.0 - vUv.x);
        float top = 1.0 - smoothstep(0.0, 0.011, 1.0 - vUv.y);
        float bottom = 1.0 - smoothstep(0.0, 0.014, vUv.y);
        float shadow = max(max(left * 0.085, right * 0.035), max(top * 0.055, bottom * 0.08));
        gl_FragColor = vec4(0.12, 0.13, 0.115, shadow);
      }
    `,
  })
  const shadow = new THREE.Mesh(screen.geometry.clone(), material)
  shadow.name = 'SCREEN_RECESS_SHADOW'
  shadow.position.copy(screen.position)
  shadow.rotation.copy(screen.rotation)
  shadow.scale.copy(screen.scale)
  shadow.position.z += 0.0001
  shadow.renderOrder = 2
  screen.parent.add(shadow)
  return shadow
}

export function createMatteScreenFinish(screen) {
  const material = new THREE.ShaderMaterial({
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    uniforms: {
      softboxDirection: { value: new THREE.Vector3(...PRIMARY_SOFTBOX.direction).normalize() },
      softboxStrength: { value: 0.24 },
      softboxHalfWidth: { value: PRIMARY_SOFTBOX.halfWidth },
      softboxHalfHeight: { value: PRIMARY_SOFTBOX.halfHeight },
      softboxFeather: { value: 0.035 },
      secondarySoftboxDirection: {
        value: new THREE.Vector3(...SECONDARY_SOFTBOX.direction).normalize(),
      },
      secondarySoftboxStrength: { value: 0.2 },
      secondarySoftboxHalfWidth: { value: SECONDARY_SOFTBOX.halfWidth },
      secondarySoftboxHalfHeight: { value: SECONDARY_SOFTBOX.halfHeight },
      secondarySoftboxFeather: { value: 0.03 },
    },
    vertexShader: `
      varying vec2 vUv;
      varying vec3 vWorldPosition;
      varying vec3 vWorldNormal;
      void main() {
        vUv = uv;
        vec4 worldPosition = modelMatrix * vec4(position, 1.0);
        vWorldPosition = worldPosition.xyz;
        vWorldNormal = normalize(mat3(modelMatrix) * normal);
        gl_Position = projectionMatrix * viewMatrix * worldPosition;
      }
    `,
    fragmentShader: `
      varying vec2 vUv;
      varying vec3 vWorldPosition;
      varying vec3 vWorldNormal;
      uniform vec3 softboxDirection;
      uniform float softboxStrength;
      uniform float softboxHalfWidth;
      uniform float softboxHalfHeight;
      uniform float softboxFeather;
      uniform vec3 secondarySoftboxDirection;
      uniform float secondarySoftboxStrength;
      uniform float secondarySoftboxHalfWidth;
      uniform float secondarySoftboxHalfHeight;
      uniform float secondarySoftboxFeather;

      float softboxMask(
        vec3 reflectedView,
        vec3 direction,
        float halfWidth,
        float halfHeight,
        float feather
      ) {
        vec3 lightDirection = normalize(direction);
        vec3 lightRight = normalize(cross(vec3(0.0, 1.0, 0.0), lightDirection));
        vec3 lightUp = normalize(cross(lightDirection, lightRight));
        vec3 reflectedOffset = reflectedView - lightDirection;
        float horizontalOffset = abs(dot(reflectedOffset, lightRight));
        float verticalOffset = abs(dot(reflectedOffset, lightUp));
        float horizontalMask = 1.0 - smoothstep(halfWidth, halfWidth + feather, horizontalOffset);
        float verticalMask = 1.0 - smoothstep(halfHeight, halfHeight + feather, verticalOffset);
        return horizontalMask * verticalMask;
      }

      float hash(vec2 point) {
        return fract(sin(dot(point, vec2(127.1, 311.7))) * 43758.5453123);
      }

      void main() {
        vec3 normal = normalize(vWorldNormal);
        vec3 viewDirection = normalize(cameraPosition - vWorldPosition);
        vec3 reflectedView = reflect(-viewDirection, normal);
        float grain = hash(floor(vUv * vec2(800.0, 480.0)));
        float fresnel = pow(1.0 - abs(dot(normal, viewDirection)), 2.6);
        float softbox = softboxMask(
          reflectedView,
          softboxDirection,
          softboxHalfWidth,
          softboxHalfHeight,
          softboxFeather
        );
        float secondarySoftbox = softboxMask(
          reflectedView,
          secondarySoftboxDirection,
          secondarySoftboxHalfWidth,
          secondarySoftboxHalfHeight,
          secondarySoftboxFeather
        );
        float edgeFade = smoothstep(0.0, 0.035, vUv.x) * smoothstep(0.0, 0.035, 1.0 - vUv.x)
          * smoothstep(0.0, 0.055, vUv.y) * smoothstep(0.0, 0.055, 1.0 - vUv.y);
        float reflectionAlpha = softbox * softboxStrength
          + secondarySoftbox * secondarySoftboxStrength;
        float alpha = 0.004 + grain * 0.004 + fresnel * 0.024 + reflectionAlpha * edgeFade;
        gl_FragColor = vec4(vec3(0.92, 0.95, 0.91), alpha);
      }
    `,
  })
  material.name = 'matte-epaper-protective-finish'

  const finish = new THREE.Mesh(screen.geometry.clone(), material)
  finish.name = 'SCREEN_FINISH'
  finish.position.copy(screen.position)
  finish.rotation.copy(screen.rotation)
  finish.scale.copy(screen.scale)
  finish.position.z += 0.0002
  finish.renderOrder = 3
  screen.parent.add(finish)
  return finish
}

export function createEpaperMaterial(texture) {
  return new THREE.MeshPhysicalMaterial({
    map: texture,
    // Tint only the physical 3D material. The mapped 800 × 480 pixels still
    // come unchanged from the shared native/WASM renderer.
    color: 0xc2c6bf,
    roughness: 0.92,
    metalness: 0,
    ior: 1.46,
    specularIntensity: 0.12,
    clearcoat: 0.01,
    clearcoatRoughness: 0.96,
    envMapIntensity: 0.14,
    toneMapped: true,
    name: 'live-forecast',
  })
}

export function createEpaperBacking(screen) {
  // Continue the same rendered frame underneath the bezel. Reuse the fitted
  // screen geometry and scale so the backing, finish and recess shadow form
  // one aligned stack without exposing a plain strip around the active frame.
  const material = createEpaperMaterial(screen.material.map ?? null)
  material.name = 'epaper-backing'
  const backing = new THREE.Mesh(screen.geometry.clone(), material)
  backing.name = 'SCREEN_BACKING'
  backing.position.copy(screen.position)
  backing.rotation.copy(screen.rotation)
  backing.scale.copy(screen.scale)
  backing.position.z -= 0.00006
  screen.parent.add(backing)
  return backing
}
