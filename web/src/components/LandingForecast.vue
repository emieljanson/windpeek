<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { siteVariant } from '../marketing/siteVariant'
import { createRendererInput } from '../renderer/rendererInput'
import { landingDisplayConfiguration } from '../marketing/landingDisplay'
import { LANDING_HERO_PRESENTATION } from '../marketing/landingHeroPresentation'
import { RENDERER_DISPLAYS } from '../renderer/contract'
import { loadSharedRenderer } from '../renderer/sharedRenderer'
import { useConfiguratorStore } from '../stores/configurator'
import { createProjectiveScreen } from '../marketing/projectiveScreen'
import { brouwersdamForecast, brouwersdamTide } from '../fixtures/brouwersdam'
import { brouwersdamSwell } from '../fixtures/brouwersdamSwell'

const { width, height, corners, finish } = LANDING_HERO_PRESENTATION
const variant = siteVariant()
const store = useConfiguratorStore()
const canvas = ref(null)
const screenReady = ref(false)
let renderer
let projectiveScreen
let unmounted = false

function dispose() {
  renderer?.dispose()
  renderer = undefined
  projectiveScreen?.dispose()
  projectiveScreen = undefined
  screenReady.value = false
}

function drawForecast() {
  const forecast = store.forecast
  if (!renderer || !projectiveScreen || !forecast) return
  const completeWind = store.forecastSource !== 'demo' && forecast.spotId === store.selectedSpotId &&
    forecast.days.length === 5 && forecast.days.every(day =>
      day.samples.length === 5 && day.samples.every(sample => sample.available))
  const completeSwell = variant.id !== 'swell' || (store.swell?.available &&
    store.swell.spotId === forecast.spotId && store.swell.timezone === forecast.timezone &&
    forecast.days.every(day => day.samples.every(sample => store.swell.samples.some(wave =>
      wave.localDate === day.localDate && wave.time === sample.time && wave.heightCm >= 0 &&
      (wave.heightCm === 0 || (wave.periodTenths > 0 && wave.destinationDegrees >= 0))))))
  // Keep the example coherent: never combine live wind with invented waves.
  const example = !completeWind || !completeSwell
  try {
    const input = createRendererInput(example ? brouwersdamForecast : forecast,
      landingDisplayConfiguration(example ? brouwersdamTide : store.tide, variant,
        example ? brouwersdamSwell : store.swell, example ? 'ready' : store.swellStatus))
    projectiveScreen.setFrame(renderer.renderPreviewForDisplay(input, RENDERER_DISPLAYS.E1002_SPECTRA6))
    projectiveScreen.draw(corners, finish)
    screenReady.value = true
  } catch { dispose() }
}

watch(
  [() => store.forecastRevision, () => store.forecastSource, () => store.selectedSpotId,
    () => store.tide, () => store.swell, () => store.swellStatus],
  drawForecast,
)

onMounted(async () => {
  void store.initializeForecast()
  void store.initializeTide()
  if (variant.id === 'swell') {
    store.swellFocus = true
    void store.refreshSwell()
  }
  void store.initializeNearbyDefault()

  try {
    projectiveScreen = createProjectiveScreen(canvas.value)
    renderer = await loadSharedRenderer()
    if (unmounted) return dispose()
    drawForecast()
  } catch { dispose() }
})

onBeforeUnmount(() => {
  unmounted = true
  dispose()
})
</script>

<template>
  <canvas
    ref="canvas"
    :width="width"
    :height="height"
    :class="{ 'is-ready': screenReady }"
    :data-forecast-spot="store.forecast?.spotId"
    :data-forecast-revision="store.forecastRevision"
    aria-hidden="true"
  />
</template>
