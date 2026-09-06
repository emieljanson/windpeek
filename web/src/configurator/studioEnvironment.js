import * as THREE from 'three'

function createReflectionCard(name, color, width, height, position) {
  const material = new THREE.MeshBasicMaterial({
    color,
    side: THREE.DoubleSide,
    toneMapped: false,
  })
  const card = new THREE.Mesh(new THREE.PlaneGeometry(width, height), material)
  card.name = name
  card.position.set(...position)
  card.lookAt(0, 0, 0)
  return card
}

export function createProductStudioScene(palette = {}) {
  const studio = new THREE.Scene()
  studio.name = 'WINDSCOUT_REFLECTION_STUDIO'
  studio.background = new THREE.Color(palette.background ?? 0xc0c7cf)

  studio.add(
    createReflectionCard('STUDIO_KEY_SOFTBOX', palette.key ?? 0xfffbf3, palette.keyWidth ?? 3.2, 1.9, [-2.4, 1.05, 3.1]),
    createReflectionCard('STUDIO_TOP_SOFTBOX', palette.top ?? 0xf5f7fb, 2.8, 1.25, [0.1, 3.2, 0.15]),
    createReflectionCard('STUDIO_RIM_STRIP', palette.rim ?? 0xd9e0e8, palette.rimWidth ?? 0.55, 2.5, [2.8, 0.35, -1.4]),
    createReflectionCard('STUDIO_DARK_FLAG', palette.dark ?? 0x252b28, 0.68, 2.1, [1.85, 0.15, 2.7]),
    createReflectionCard('STUDIO_BACKDROP', palette.backdrop ?? 0xd2d7dc, 5.5, 3.5, [0, 0.2, -3.7]),
  )

  const floor = createReflectionCard('STUDIO_FLOOR', 0xc8d0d8, 5.5, 5.5, [0, -1.65, 0])
  studio.add(floor)
  return studio
}

export function createProductStudioEnvironment(renderer, palette) {
  const studio = createProductStudioScene(palette)
  const generator = new THREE.PMREMGenerator(renderer)
  const environment = generator.fromScene(studio, 0.04)

  studio.traverse((child) => {
    child.geometry?.dispose?.()
    child.material?.dispose?.()
  })
  generator.dispose()
  return environment
}
