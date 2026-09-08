<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { publicAssetUrl } from '../assets/publicAssetUrl'
import { createRendererInput } from '../renderer/rendererInput'
import { landingDisplayConfiguration } from '../marketing/landingDisplay'
import { LANDING_HERO_PRESENTATION } from '../marketing/landingHeroPresentation'
import { RENDERER_DISPLAYS } from '../renderer/contract'
import { loadSharedRenderer } from '../renderer/sharedRenderer'
import { useConfiguratorStore } from '../stores/configurator'
import { createProjectiveScreen } from '../marketing/projectiveScreen'

const {
  width: WIDTH,
  height: HEIGHT,
  corners: SCREEN_CORNERS,
  finish: SCREEN_FINISH,
  framing: HERO_FRAMING,
} = LANDING_HERO_PRESENTATION
const heroImage = publicAssetUrl(LANDING_HERO_PRESENTATION.imageFallback)
const heroImageWebp = publicAssetUrl(LANDING_HERO_PRESENTATION.imageWebp)
const heroImageSrcset = [
  ...LANDING_HERO_PRESENTATION.responsiveWebp.map(({ image, width }) => `${publicAssetUrl(image)} ${width}w`),
  `${heroImageWebp} ${WIDTH * 2}w`,
].join(', ')
// Match the 642px scene / 24px viewport gutter, including the calibrated zoom.
const zoomedSize = size => Math.round(size * HERO_FRAMING.zoom * 100) / 100
const heroImageSizes = `(max-width: 666px) calc(${zoomedSize(100)}vw - ${zoomedSize(24)}px), ${zoomedSize(642)}px`
const heroMediaStyle = Object.freeze({
  '--hero-zoom': HERO_FRAMING.zoom,
  '--hero-focus-x': `${-HERO_FRAMING.focusX}%`,
  '--hero-focus-y': `${-HERO_FRAMING.focusY}%`,
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
    renderer?.dispose()
    renderer = undefined
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
    <a class="hero-link" href="?configure" aria-label="Open the Windpeek configurator">
      <div class="hero-scene">
        <div class="hero-media" :style="heroMediaStyle">
          <picture>
            <source :srcset="heroImageSrcset" :sizes="heroImageSizes" type="image/webp">
            <img
              :src="heroImage"
              width="1672"
              height="941"
              alt="Windpeek on a yellow designer sideboard showing a five-day e-ink forecast"
              fetchpriority="high"
            >
          </picture>
          <canvas ref="canvas" :width="WIDTH" :height="HEIGHT" aria-hidden="true" />
        </div>
      </div>
    </a>
  </figure>
</template>
