import { LoadingManager } from 'three'
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js'
import { E1002_MODEL } from '../assets/e1002'
import { E1003_MODEL } from '../assets/e1003'
import { BOARD_IDS } from '../config/configuration'

export const MODEL_LOAD_TIMEOUT_MS = 15_000

function modelForBoard(boardId) {
  if (boardId === BOARD_IDS.E1001) return E1002_MODEL
  if (boardId === BOARD_IDS.E1002) return E1002_MODEL
  if (boardId === BOARD_IDS.E1003) return E1003_MODEL
  throw new TypeError(`Unsupported reTerminal model: ${boardId}`)
}

export function findMissingModelRoles(scene, modelDefinition = E1002_MODEL) {
  return modelDefinition.requiredRoles.filter((role) => !scene.getObjectByName(role))
}

export function hideDeviceStand(scene) {
  const stand = scene.getObjectByName('STAND')
  if (!stand) return false
  // Keep the hidden object attached so the normal scene disposal path still
  // releases its imported geometry and materials.
  stand.visible = false
  return true
}

async function loadModel(definition, label, {
  loaderFactory = (manager) => new GLTFLoader(manager),
  timeoutMs = MODEL_LOAD_TIMEOUT_MS,
} = {}) {
  const manager = new LoadingManager()
  const loader = loaderFactory(manager)
  let timeout
  const timeoutPromise = new Promise((_, reject) => {
    timeout = setTimeout(() => {
      manager.abort()
      reject(new Error('The 3D model took too long to load.'))
    }, timeoutMs)
  })

  let gltf
  try {
    gltf = await Promise.race([loader.loadAsync(definition.url), timeoutPromise])
  } finally {
    clearTimeout(timeout)
  }
  const missingRoles = findMissingModelRoles(gltf.scene, definition)
  if (missingRoles.length) {
    throw new Error(`${label} model is missing scene roles: ${missingRoles.join(', ')}`)
  }
  return gltf.scene
}

export function loadE1002Model(options) {
  return loadModel(E1002_MODEL, 'E1002', options)
}

export function loadDeviceModel(boardId, options) {
  const definition = modelForBoard(boardId)
  return loadModel(definition, 'Device', options)
}
