import { describe, expect, it, vi } from 'vitest'
import { E1002_MODEL } from '../src/assets/e1002'
import { E1003_MODEL } from '../src/assets/e1003'
import { BOARD_IDS } from '../src/config/configuration'
import {
  findMissingModelRoles,
  hideDeviceStand,
  loadDeviceModel,
  loadE1002Model,
} from '../src/configurator/modelLoader'

function modelWithRoles(roles) {
  return {
    getObjectByName(name) {
      return roles.includes(name) ? { name } : undefined
    },
  }
}

describe('E1002 model loader contract', () => {
  it('accepts a scene with all independently addressable roles', () => {
    expect(findMissingModelRoles(modelWithRoles(E1002_MODEL.requiredRoles))).toEqual([])
  })

  it('reports every missing role before the scene becomes interactive', () => {
    expect(findMissingModelRoles(modelWithRoles(['BODY', 'SCREEN']))).toEqual(['CONTROLS', 'PORTS', 'STAND'])
  })

  it('hides the printed stand while retaining it for model cleanup', () => {
    const stand = { visible: true }
    const model = { getObjectByName: vi.fn(() => stand) }

    expect(hideDeviceStand(model)).toBe(true)
    expect(stand.visible).toBe(false)
  })

  it('hides the optional printed stand when a source model contains one', () => {
    const e1002Stand = { visible: true }

    expect(hideDeviceStand({ getObjectByName: () => e1002Stand })).toBe(true)
    expect(hideDeviceStand({ getObjectByName: () => undefined })).toBe(false)
    expect(e1002Stand.visible).toBe(false)
  })

  it('aborts a model request that never settles', async () => {
    vi.useFakeTimers()
    const request = loadE1002Model({
      loaderFactory: () => ({ loadAsync: () => new Promise(() => {}) }),
      timeoutMs: 25,
    })
    const expectation = expect(request).rejects.toThrow('took too long')
    await vi.advanceTimersByTimeAsync(25)
    await expectation
    vi.useRealTimers()
  })

  it.each([
    [BOARD_IDS.E1001, E1002_MODEL],
    [BOARD_IDS.E1002, E1002_MODEL],
    [BOARD_IDS.E1003, E1003_MODEL],
  ])('loads the model that belongs to %s', async (boardId, definition) => {
    const scene = modelWithRoles(definition.requiredRoles)
    const loadAsync = vi.fn(async () => ({ scene }))

    await expect(loadDeviceModel(boardId, {
      loaderFactory: () => ({ loadAsync }),
    })).resolves.toBe(scene)
    expect(loadAsync).toHaveBeenCalledWith(definition.url)
  })

  it('rejects unsupported board IDs before loading an asset', () => {
    expect(() => loadDeviceModel('unsupported-board')).toThrow('Unsupported reTerminal model')
  })
})
