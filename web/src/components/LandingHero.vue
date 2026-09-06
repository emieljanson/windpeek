<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { publicAssetUrl } from '../assets/publicAssetUrl'
import { createRendererInput } from '../configurator/screenTexture'
import { landingDisplayConfiguration } from '../marketing/landingDisplay'
import { RENDERER_DISPLAYS } from '../renderer/contract'
import { loadSharedRenderer } from '../renderer/sharedRenderer'
import { useConfiguratorStore } from '../stores/configurator'
import { createProjectiveScreen } from '../marketing/projectiveScreen'

const WIDTH = 1672
const HEIGHT = 941
const heroImage = publicAssetUrl('marketing/windscout-hero-yellow-v2.png')
const heroImageWebp = publicAssetUrl('marketing/windscout-hero-yellow-v2.webp')
const SCREEN_CORNERS = Object.freeze([
  { x: 606, y: 285 },
  { x: 1228, y: 282 },
  { x: 1230, y: 654 },
  { x: 604, y: 654 },
])
const SCREEN_FINISH = Object.freeze({
  opacity: 1,
  brightness: 0.79,
  contrast: 1,
  shadowSize: 0.018,
  shadowOpacity: 0.06,
  reflection: 0.1,
  reflectionColor: '#ffe0c2',
})

const store = useConfiguratorStore()
const canvas = ref(null)
const screenReady = ref(false)
let renderer
let projectiveScreen
let unmounted = false

function drawForecast(forecast) {
  if (!renderer || !projectiveScreen || !forecast) return
  const input = createRendererInput(forecast, landingDisplayConfiguration(store.tide))
  const frame = renderer.renderPreviewForDisplay(input, RENDERER_DISPLAYS.E1002_SPECTRA6)
  projectiveScreen.setFrame(frame)
  projectiveScreen.draw(SCREEN_CORNERS, SCREEN_FINISH)
  screenReady.value = true
}

watch(
  [() => store.forecastRevision, () => store.tide],
  () => drawForecast(store.forecast),
)

onMounted(async () => {
  void store.initializeForecast()
  void store.initializeTide()
  void store.initializeNearbyDefault()

  try {
    projectiveScreen = createProjectiveScreen(canvas.value)
    renderer = await loadSharedRenderer()
    if (unmounted) {
      renderer.dispose()
      renderer = undefined
      return
    }
    drawForecast(store.forecast)
  } catch {
    projectiveScreen?.dispose()
    projectiveScreen = undefined
    screenReady.value = false
  }
})

onBeforeUnmount(() => {
  unmounted = true
  renderer?.dispose()
  projectiveScreen?.dispose()
})
</script>

<template>
  <figure
    class="landing-hero"
    :class="{ 'is-ready': screenReady }"
    :data-forecast-spot="store.forecast?.spotId"
    :data-forecast-revision="store.forecastRevision"
  >
    <a class="hero-link" href="?configure" aria-label="Open the Windscout configurator">
      <div class="hero-scene">
        <div class="hero-media">
          <picture>
            <source :srcset="heroImageWebp" type="image/webp">
            <img
              :src="heroImage"
              width="1672"
              height="941"
              alt="Windscout on a yellow designer sideboard showing a five-day e-ink forecast"
              fetchpriority="high"
            >
          </picture>
          <canvas ref="canvas" :width="WIDTH" :height="HEIGHT" aria-hidden="true" />
        </div>
      </div>
    </a>
  </figure>
</template>
