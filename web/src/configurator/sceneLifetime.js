export function createResourceLifetime() {
  let active = true

  return {
    get active() {
      return active
    },
    adopt(resource, dispose) {
      if (active) return true
      dispose(resource)
      return false
    },
    cancel() {
      active = false
    },
  }
}

export function disposeSceneObject(object) {
  object?.traverse((child) => {
    child.geometry?.dispose?.()
    const materials = Array.isArray(child.material) ? child.material : [child.material]
    materials.filter(Boolean).forEach((material) => material.dispose?.())
  })
}

export function disposeSceneResources({
  orbitRendering, controls, screenSource, disposeSurface, disposeRearMarkings,
  environmentTarget, keyLight, gtaoPass, smaaPass, outputPass, composer,
  model, scene, usbCable, renderer,
}) {
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
  disposeSceneObject(model)
  for (const name of ['SURFACE_GRID', 'STUDIO_LIT_FLOOR', 'PHYSICAL_SHADOW_LAYER', 'CONTACT_OCCLUSION'])
    disposeSceneObject(scene?.getObjectByName(name))
  usbCable?.dispose()
  renderer?.dispose()
  renderer?.forceContextLoss()
  renderer?.domElement.remove()
}
