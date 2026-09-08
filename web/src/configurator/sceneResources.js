// Keep ownership local until both independent loads succeed. If either fails,
// release its sibling even when that sibling finishes after the error.
export async function loadSceneResources({ lifetime, loadModel, loadScreen, disposeModel }) {
  const acquired = []
  let failed = false

  async function load(loader, dispose) {
    const resource = await loader()
    if (failed || !lifetime.active) {
      dispose(resource)
      return null
    }
    acquired.push({ resource, dispose })
    return resource
  }

  function release() {
    for (const { resource, dispose } of acquired.splice(0)) dispose(resource)
  }

  try {
    const [model, screen] = await Promise.all([
      load(loadModel, disposeModel),
      load(loadScreen, (source) => source.dispose()),
    ])
    if (!lifetime.active) {
      release()
      return null
    }
    return { model, screen }
  } catch (error) {
    failed = true
    release()
    throw error
  }
}
