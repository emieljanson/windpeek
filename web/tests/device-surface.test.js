import { describe, expect, it } from 'vitest'
import { readFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import * as THREE from 'three'
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js'
import {
  addSurfaceProjectionUvs,
  createFrontPanelReflection,
  createEpaperBacking,
  createEpaperMaterial,
  createPowderCoatNormalMap,
  createPowderCoatRoughnessMap,
  createMatteScreenFinish,
  createScreenRecessShadow,
  enhanceDeviceSurface,
  fitScreenUnderBezel,
  SCREEN_BEZEL_OVERSCAN,
} from '../src/configurator/deviceSurface'

describe('reTerminal product surfaces', () => {
  it.each([
    ['E1001', 'e1002'],
    ['E1002', 'e1002'],
    ['E1003', 'e1003'],
  ])('applies the studio finish to the actual %s model', async (_, asset) => {
    const source = await readFile(resolve(process.cwd(), `public/devices/${asset}/${asset}.glb`))
    const buffer = source.buffer.slice(source.byteOffset, source.byteOffset + source.byteLength)
    const { scene } = await new GLTFLoader().parseAsync(buffer, '')
    const dispose = enhanceDeviceSurface(scene, { capabilities: { getMaxAnisotropy: () => 4 } })
    try {
      const meshes = scene.getObjectsByProperty('isMesh', true)
      const materials = new Map(meshes.map((mesh) => [mesh.material.name, mesh.material]))
      const coatNames = asset === 'e1003'
        ? ['enclosure-white-powder-coat', 'rear-service-cover', 'control-surround-white', 'front-lower-cover']
        : ['enclosure-white-powder-coat']
      for (const name of coatNames) {
        const material = materials.get(name)
        expect(material, name).toBeDefined()
        expect(material.normalMap?.name).toBe('powder-coat-micro-normal')
        expect(material.roughness).toBeCloseTo(0.78)
        expect(material.metalness).toBe(0)
      }
      const frontName = asset === 'e1003' ? 'front-satin-trim' : 'front-satin-plastic'
      const fronts = meshes.filter((mesh) => mesh.material.name === frontName)
      expect(fronts.length).toBeGreaterThan(0)
      for (const front of fronts) {
        expect(front.material.roughness).toBeCloseTo(0.12)
        expect(front.material.clearcoat).toBeCloseTo(0.65)
        expect(front.getObjectByName('FRONT_PANEL_REFLECTION')).toBeDefined()
      }
    } finally {
      dispose()
      scene.traverse((object) => {
        object.geometry?.dispose()
        object.material?.dispose()
      })
    }
  })
  it('retains the moving front-panel reflection in the standard studio', () => {
    const model = new THREE.Group()
    const panel = new THREE.Mesh(
      new THREE.BoxGeometry(0.176, 0.12, 0.001),
      new THREE.MeshPhysicalMaterial({ name: 'front-satin-plastic' }),
    )
    model.add(panel)
    const dispose = enhanceDeviceSurface(model, {
      capabilities: { getMaxAnisotropy: () => 4 },
    })
    try {
      expect(panel.getObjectByName('FRONT_PANEL_REFLECTION')).toBeDefined()
    } finally {
      dispose()
      expect(panel.getObjectByName('FRONT_PANEL_REFLECTION')).toBeUndefined()
      panel.geometry.dispose()
      panel.material.dispose()
    }
  })
  it('builds a tileable, linear-space powder-coat normal map', () => {
    const texture = createPowderCoatNormalMap(16)

    expect(texture.image.width).toBe(16)
    expect(texture.image.height).toBe(16)
    expect(texture.image.data).toHaveLength(16 * 16 * 4)
    expect(texture.wrapS).toBe(THREE.RepeatWrapping)
    expect(texture.wrapT).toBe(THREE.RepeatWrapping)
    expect(texture.colorSpace).toBe(THREE.NoColorSpace)
    expect(texture.generateMipmaps).toBe(true)
    expect(texture.minFilter).toBe(THREE.LinearMipmapLinearFilter)
    expect(texture.magFilter).toBe(THREE.LinearFilter)
    texture.dispose()
  })

  it('adds fine matte variation to the powder-coat highlights', () => {
    const texture = createPowderCoatRoughnessMap(16)

    expect(texture.name).toBe('powder-coat-micro-roughness')
    expect(texture.image.data).toHaveLength(16 * 16 * 4)
    expect(texture.wrapS).toBe(THREE.RepeatWrapping)
    expect(texture.wrapT).toBe(THREE.RepeatWrapping)
    texture.dispose()
  })

  it('adds surface-scale UVs to CAD geometry without replacing existing UVs', () => {
    const geometry = new THREE.BoxGeometry(0.176, 0.12, 0.017)
    geometry.deleteAttribute('uv')
    addSurfaceProjectionUvs(geometry)
    const firstUv = geometry.attributes.uv

    expect(firstUv).toBeDefined()
    expect(firstUv.count).toBe(geometry.attributes.position.count)

    addSurfaceProjectionUvs(geometry)
    expect(geometry.attributes.uv).toBe(firstUv)
    geometry.dispose()
  })

  it('applies the same micro-relief to every powder-coated housing panel', () => {
    const model = new THREE.Group()
    const enclosureMaterial = new THREE.MeshPhysicalMaterial({ name: 'enclosure-white-powder-coat' })
    const rearMaterial = new THREE.MeshPhysicalMaterial({ name: 'rear-service-cover' })
    const panelMaterial = new THREE.MeshPhysicalMaterial({ name: 'front-satin-plastic' })
    const enclosure = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), enclosureMaterial)
    const rear = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), rearMaterial)
    const panel = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), panelMaterial)
    model.add(enclosure, rear, panel)

    const dispose = enhanceDeviceSurface(model, { capabilities: { getMaxAnisotropy: () => 8 } })

    expect(enclosureMaterial.normalMap?.name).toBe('powder-coat-micro-normal')
    expect(enclosureMaterial.normalScale.x).toBeCloseTo(0.3)
    expect(enclosureMaterial.roughnessMap?.name).toBe('powder-coat-micro-roughness')
    expect(enclosureMaterial.roughness).toBeCloseTo(0.78)
    expect(rearMaterial.normalMap?.name).toBe('powder-coat-micro-normal')
    expect(rearMaterial.roughnessMap?.name).toBe('powder-coat-micro-roughness')
    expect(rearMaterial.roughness).toBeCloseTo(enclosureMaterial.roughness)
    expect(rearMaterial.color.getHex()).toBe(enclosureMaterial.color.getHex())
    expect(rearMaterial.clearcoat).toBe(enclosureMaterial.clearcoat)
    expect(rearMaterial.clearcoatRoughness).toBe(enclosureMaterial.clearcoatRoughness)
    expect(panelMaterial.normalMap).toBeNull()
    expect(panelMaterial.roughness).toBeCloseTo(0.12)
    expect(panelMaterial.ior).toBeCloseTo(1.55)
    expect(panelMaterial.specularIntensity).toBeCloseTo(1)
    expect(panelMaterial.clearcoat).toBeCloseTo(0.65)
    expect(panelMaterial.clearcoatRoughness).toBeCloseTo(0.055)
    expect(panelMaterial.envMapIntensity).toBeCloseTo(1.35)
    expect(panelMaterial.color.getHex()).toBe(0xe6e7e3)
    expect(panel.getObjectByName('FRONT_PANEL_REFLECTION')).toBeDefined()

    dispose()
    enclosure.geometry.dispose()
    rear.geometry.dispose()
    panel.geometry.dispose()
    enclosureMaterial.dispose()
    rearMaterial.dispose()
    panelMaterial.dispose()
  })

  it('renders the live forecast as reflective e-paper instead of an emissive screen', () => {
    const texture = new THREE.Texture()
    const material = createEpaperMaterial(texture)

    expect(material).toBeInstanceOf(THREE.MeshPhysicalMaterial)
    expect(material.map).toBe(texture)
    expect(material.color.getHex()).toBe(0xc2c6bf)
    expect(material.emissive.getHex()).toBe(0x000000)
    expect(material.roughness).toBeGreaterThan(0.8)
    expect(material.toneMapped).toBe(true)

    material.dispose()
    texture.dispose()
  })

  it('gives the matte screen a view-dependent softbox reflection', () => {
    const root = new THREE.Group()
    const screen = new THREE.Mesh(new THREE.PlaneGeometry(0.159, 0.0954), new THREE.MeshBasicMaterial())
    root.add(screen)

    const finish = createMatteScreenFinish(screen)

    expect(finish.material.uniforms.softboxDirection.value).toBeInstanceOf(THREE.Vector3)
    expect(finish.material.uniforms.softboxStrength.value).toBeGreaterThanOrEqual(0.2)
    expect(finish.material.uniforms.softboxHalfWidth.value).toBeLessThan(0.12)
    expect(finish.material.uniforms.softboxHalfHeight.value)
      .toBeGreaterThan(finish.material.uniforms.softboxHalfWidth.value * 2)
    expect(finish.material.uniforms.softboxHalfWidth.value).toBeCloseTo(0.055)
    expect(finish.material.uniforms.softboxHalfHeight.value).toBeCloseTo(0.15)
    expect(finish.material.uniforms.softboxDirection.value.y).toBeLessThan(0)
    expect(finish.material.uniforms.softboxDirection.value.x).toBeGreaterThan(0)
    expect(finish.material.uniforms.secondarySoftboxDirection.value.x).toBeLessThan(0)
    expect(finish.material.uniforms.secondarySoftboxDirection.value.y).toBeLessThan(0)
    expect(finish.material.uniforms.secondarySoftboxStrength.value).toBeGreaterThan(0.1)
    expect(finish.material.fragmentShader).toContain('uniform vec3 softboxDirection;')
    expect(finish.material.fragmentShader).toContain('uniform vec3 secondarySoftboxDirection;')
    expect(finish.material.fragmentShader).toContain('uniform float softboxStrength;')
    root.traverse((child) => {
      child.geometry?.dispose?.()
      child.material?.dispose?.()
    })
  })

  it('fits the complete e-paper stack underneath the rounded bezel', () => {
    const root = new THREE.Group()
    const texture = new THREE.Texture()
    const screen = new THREE.Mesh(
      new THREE.PlaneGeometry(0.159, 0.0954),
      new THREE.MeshBasicMaterial({ map: texture }),
    )
    screen.position.set(0, 0.066, 0.00278)
    root.add(screen)

    fitScreenUnderBezel(screen)
    const backing = createEpaperBacking(screen)
    const size = backing.geometry.parameters

    expect(screen.scale.x).toBeCloseTo(SCREEN_BEZEL_OVERSCAN)
    expect(screen.scale.y).toBeCloseTo(SCREEN_BEZEL_OVERSCAN)
    expect(size.width * backing.scale.x).toBeCloseTo(0.159 * SCREEN_BEZEL_OVERSCAN)
    expect(size.height * backing.scale.y).toBeCloseTo(0.0954 * SCREEN_BEZEL_OVERSCAN)
    expect(backing.position.y).toBeCloseTo(screen.position.y)
    expect(backing.position.z).toBeLessThan(screen.position.z)
    expect(backing.scale.equals(screen.scale)).toBe(true)
    expect(backing.material.map).toBe(texture)

    root.traverse((child) => {
      child.geometry?.dispose?.()
      child.material?.dispose?.()
    })
    texture.dispose()
  })

  it('clips the leading UI equally beneath every visible bezel edge', () => {
    const horizontalClip = (0.159 * SCREEN_BEZEL_OVERSCAN - 0.1602) / 2
    const verticalClip = (0.0954 * SCREEN_BEZEL_OVERSCAN - 0.0949) / 2

    expect(horizontalClip).toBeGreaterThan(0)
    expect(horizontalClip).toBeCloseTo(verticalClip, 7)
    expect(horizontalClip).toBeCloseTo(0.001525, 6)
  })

  it('adds a hard, view-dependent white softbox to the glossy front panel', () => {
    const panel = new THREE.Mesh(
      new THREE.BoxGeometry(1, 1, 0.1),
      new THREE.MeshPhysicalMaterial({ name: 'front-satin-plastic' }),
    )
    const reflection = createFrontPanelReflection(panel)

    expect(reflection.name).toBe('FRONT_PANEL_REFLECTION')
    expect(reflection.material.transparent).toBe(true)
    expect(reflection.material.toneMapped).toBe(false)
    expect(reflection.material.uniforms.softboxStrength.value).toBeGreaterThan(0.45)
    expect(reflection.material.uniforms.softboxDirection.value.x).toBeGreaterThan(0)
    expect(reflection.material.uniforms.secondarySoftboxDirection.value.x).toBeLessThan(0)
    expect(reflection.material.uniforms.secondarySoftboxDirection.value.y).toBeLessThan(0)
    expect(reflection.material.uniforms.secondarySoftboxStrength.value).toBeGreaterThan(0.25)
    expect(reflection.material.fragmentShader).toContain('softbox * softboxStrength')
    expect(reflection.material.fragmentShader)
      .toContain('secondarySoftbox * secondarySoftboxStrength')
    reflection.material.dispose()
    panel.geometry.dispose()
    panel.material.dispose()
  })

  it('keeps both softbox reflections aligned across the screen and front panel', () => {
    const root = new THREE.Group()
    const screen = new THREE.Mesh(
      new THREE.PlaneGeometry(0.159, 0.0954),
      new THREE.MeshBasicMaterial(),
    )
    const panel = new THREE.Mesh(
      new THREE.BoxGeometry(1, 1, 0.1),
      new THREE.MeshPhysicalMaterial({ name: 'front-satin-plastic' }),
    )
    root.add(screen, panel)

    const finish = createMatteScreenFinish(screen)
    const reflection = createFrontPanelReflection(panel)
    const screenUniforms = finish.material.uniforms
    const panelUniforms = reflection.material.uniforms

    expect(panelUniforms.softboxDirection.value)
      .toEqual(screenUniforms.softboxDirection.value)
    expect(panelUniforms.softboxHalfWidth.value)
      .toBe(screenUniforms.softboxHalfWidth.value)
    expect(panelUniforms.softboxHalfHeight.value)
      .toBe(screenUniforms.softboxHalfHeight.value)
    expect(panelUniforms.softboxFeather.value)
      .toBeLessThan(screenUniforms.softboxFeather.value)
    expect(panelUniforms.secondarySoftboxDirection.value)
      .toEqual(screenUniforms.secondarySoftboxDirection.value)
    expect(panelUniforms.secondarySoftboxHalfWidth.value)
      .toBe(screenUniforms.secondarySoftboxHalfWidth.value)
    expect(panelUniforms.secondarySoftboxHalfHeight.value)
      .toBe(screenUniforms.secondarySoftboxHalfHeight.value)
    expect(panelUniforms.secondarySoftboxFeather.value)
      .toBeLessThan(screenUniforms.secondarySoftboxFeather.value)

    root.traverse((child) => {
      child.geometry?.dispose?.()
      child.material?.dispose?.()
    })
  })

  it('adds a soft recess shadow without adding a physical grey rim', () => {
    const root = new THREE.Group()
    const screen = new THREE.Mesh(new THREE.PlaneGeometry(0.159, 0.0954), new THREE.MeshBasicMaterial())
    root.add(screen)

    const shadow = createScreenRecessShadow(screen)

    expect(shadow.name).toBe('SCREEN_RECESS_SHADOW')
    expect(shadow.material.name).toBe('screen-recess-shadow')
    expect(root.getObjectByName('SCREEN_GASKET')).toBeUndefined()
    root.traverse((child) => {
      child.geometry?.dispose?.()
      child.material?.dispose?.()
    })
  })
})
