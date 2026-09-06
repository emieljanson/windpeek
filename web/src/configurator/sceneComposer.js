import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js'

export function createSceneComposer(renderer) {
  const composer = new EffectComposer(renderer)
  // Canvas antialiasing does not apply to offscreen rendering. Both buffers
  // need MSAA because the composer swaps them between frames/passes.
  const samples = Math.min(4, renderer.capabilities.maxSamples)
  composer.renderTarget1.samples = samples
  composer.renderTarget2.samples = samples
  return composer
}
