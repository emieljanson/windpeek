import { describe, expect, it, vi } from 'vitest'
import { createResourceLifetime } from '../src/configurator/sceneLifetime'
import { loadSceneResources } from '../src/configurator/sceneResources'

function deferred() {
  let resolve, reject
  const promise = new Promise((yes, no) => { resolve = yes; reject = no })
  return { promise, resolve, reject }
}

function setup() {
  const modelLoad = deferred()
  const screenLoad = deferred()
  const model = { name: 'model' }
  const screen = { dispose: vi.fn() }
  const lifetime = createResourceLifetime()
  const disposeModel = vi.fn()
  const loadModel = vi.fn(() => modelLoad.promise)
  const loadScreen = vi.fn(() => screenLoad.promise)
  const loading = loadSceneResources({ lifetime, loadModel, loadScreen, disposeModel })
  return { modelLoad, screenLoad, model, screen, lifetime, disposeModel, loadModel, loadScreen, loading }
}

describe('concurrent scene resources', () => {
  it('starts both loads before either completes and transfers ownership on success', async () => {
    const task = setup()
    expect(task.loadModel).toHaveBeenCalledOnce()
    expect(task.loadScreen).toHaveBeenCalledOnce()
    task.screenLoad.resolve(task.screen)
    task.modelLoad.resolve(task.model)
    await expect(task.loading).resolves.toEqual({ model: task.model, screen: task.screen })
    expect(task.disposeModel).not.toHaveBeenCalled()
    expect(task.screen.dispose).not.toHaveBeenCalled()
  })

  for (const failed of ['model', 'screen']) {
    for (const siblingFinishesFirst of [true, false]) {
      it(`releases the sibling when ${failed} fails, sibling finishes first: ${siblingFinishesFirst}`, async () => {
        const task = setup()
        const sibling = failed === 'model' ? 'screen' : 'model'
        const error = new Error(`${failed} failed`)
        const rejection = expect(task.loading).rejects.toBe(error)
        if (siblingFinishesFirst) {
          task[`${sibling}Load`].resolve(task[sibling])
          await Promise.resolve()
        }
        task[`${failed}Load`].reject(error)
        // Failure is reported without waiting for the other download.
        await rejection
        if (!siblingFinishesFirst) {
          task[`${sibling}Load`].resolve(task[sibling])
          await Promise.resolve()
        }
        const dispose = sibling === 'model' ? task.disposeModel : task.screen.dispose
        expect(dispose).toHaveBeenCalledOnce()
      })
    }
  }

  for (const modelFinishesFirst of [true, false]) {
    it(`disposes both resources on unmount, model already loaded: ${modelFinishesFirst}`, async () => {
      const task = setup()
      if (modelFinishesFirst) {
        task.modelLoad.resolve(task.model)
        await Promise.resolve()
      }
      task.lifetime.cancel()
      task.modelLoad.resolve(task.model)
      task.screenLoad.resolve(task.screen)
      await expect(task.loading).resolves.toBeNull()
      expect(task.disposeModel).toHaveBeenCalledExactlyOnceWith(task.model)
      expect(task.screen.dispose).toHaveBeenCalledOnce()
    })
  }
})
