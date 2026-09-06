<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { createRendererInput } from '../configurator/screenTexture'
import { landingDisplayConfiguration } from '../marketing/landingDisplay'
import { RENDERER_DISPLAYS } from '../renderer/contract'
import { loadSharedRenderer } from '../renderer/sharedRenderer'
import { useConfiguratorStore } from '../stores/configurator'

const canvas = ref(null)
const ready = ref(false)
const failed = ref(false)
const store = useConfiguratorStore()
const caption = computed(() => {
  if (failed.value) return 'Forecast preview unavailable'
  if (!ready.value) return 'Loading current forecast…'
  if (store.forecastSource === 'live' || store.forecastSource === 'current') return `Live forecast for ${store.forecast.spotName}`
  if (store.forecastSource === 'cache') return `Cached forecast for ${store.forecast.spotName}`
  return `Demo forecast for ${store.forecast.spotName}`
})
let renderer

function drawForecast() {
  if (!renderer || !store.forecast) return
  const input = createRendererInput(store.forecast, landingDisplayConfiguration(store.tide))
  const frame = renderer.renderPreviewForDisplay(input, RENDERER_DISPLAYS.E1002_SPECTRA6)
  const context = canvas.value.getContext('2d')
  context.putImageData(new ImageData(new Uint8ClampedArray(frame.data), frame.width, frame.height), 0, 0)
  ready.value = true
}

watch(
  [() => store.forecastRevision, () => store.tideStatus],
  drawForecast,
)

onMounted(async () => {
  try {
    renderer = await loadSharedRenderer()
    drawForecast()
    void store.initializeForecast()
    void store.initializeTide()
  } catch {
    failed.value = true
  }
})

onBeforeUnmount(() => renderer?.dispose())
</script>

<template>
  <section class="forecast-preview" aria-label="Current Windscout forecast">
    <canvas
      ref="canvas"
      class="forecast-screen"
      width="800"
      height="480"
      role="img"
      :aria-label="`Five-day Windscout forecast for ${store.forecast?.spotName || 'Brouwersdam'}`"
    />
    <p class="forecast-caption">{{ caption }}</p>
  </section>
</template>
