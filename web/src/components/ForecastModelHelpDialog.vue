<script setup>
import { computed } from 'vue'
import { DialogClose, DialogContent, DialogDescription, DialogOverlay, DialogPortal, DialogRoot, DialogTitle } from 'reka-ui'
import { FORECAST_MODELS } from '../forecast/models'
import { SWELL_MODELS } from '../forecast/openMeteoSwell'

const props = defineProps({
  open: { type: Boolean, required: true },
  kind: { type: String, default: 'wind' },
  availableWindModelIds: { type: Array, default: () => FORECAST_MODELS.map(model => model.id) },
})
const emit = defineEmits(['update:open', 'closed'])
const isWind = computed(() => props.kind === 'wind')
const title = computed(() => isWind.value ? 'Wind models' : 'Wave models')
// Approximate API grid spacing, checked against Open-Meteo model documentation.
// Coverage describes model domains; the configurator filters regional choices by country.
const windDetails = {
  best_match: ['Varies', 'Worldwide · automatic selection'],
  ecmwf_ifs: ['9 km', 'Worldwide'],
  icon_seamless: ['2–11 km', 'Worldwide · finer in Europe'],
  ncep_gfs_seamless: ['13–25 km', 'Worldwide'],
  knmi_harmonie: ['2 km', 'Netherlands & Belgium'],
  dmi_harmonie: ['2 km', 'Central & Northern Europe'],
  met_nordic: ['1 km', 'Norway, Sweden, Finland & Denmark'],
  meteofrance_arome: ['1.5 km', 'France & surrounding area'],
  ukmo_ukv: ['2 km', 'UK & Ireland'],
  meteoswiss_icon: ['1 km', 'Switzerland & surrounding Central Europe'],
  geosphere_arome: ['2.5 km', 'Austria & surrounding Alpine region'],
  italiameteo_icon: ['2 km', 'Italy & Southern Europe'],
  chmi_aladin: ['1 km', 'Czechia'],
  noaa_hrrr: ['3 km', 'Contiguous United States'],
  canada_hrdps: ['2.5 km', 'Canada & Northern US'],
  jma_msm: ['5 km', 'Japan & surrounding region'],
  kma_ldps: ['1.5 km', 'Korean Peninsula'],
}
const waveDetails = {
  best_match: ['Varies', 'Worldwide · automatic selection'],
  meteofrance_wave: ['8 km', 'Worldwide'],
  ncep_gfswave025: ['16–25 km', 'Worldwide · 16 km between 15°S and 52.5°N where available'],
  dwd_ewam: ['5 / 25 km', 'Europe at 5 km · global GWAM at 25 km'],
}
const models = computed(() => isWind.value
  ? FORECAST_MODELS.map(model => ({ id: model.id, name: model.label, details: windDetails[model.id] }))
  : SWELL_MODELS.map(model => ({ id: model.value, name: model.label, details: waveDetails[model.value] })))
const availableModels = computed(() => models.value.filter(model => !isWind.value || props.availableWindModelIds.includes(model.id)))
const otherModels = computed(() => isWind.value ? models.value.filter(model => !props.availableWindModelIds.includes(model.id)) : [])
function restoreFocus(event) {
  event.preventDefault()
  emit('closed')
}
</script>

<template>
  <DialogRoot :open="open" @update:open="emit('update:open', $event)">
    <DialogPortal>
      <DialogOverlay class="reterminal-help__overlay" />
      <DialogContent class="reterminal-help model-help" @close-auto-focus="restoreFocus">
        <header>
          <DialogTitle class="reterminal-help__title">{{ title }}</DialogTitle>
          <DialogDescription class="reterminal-help__description">
            {{ isWind ? 'Different models can disagree. Choose one you trust for your spot, or start with Best Match.' : 'Start with Best Match, or compare models for your spot. Waves shows swell at sea, not the height of breaking waves at your beach.' }}
          </DialogDescription>
        </header>
        <section class="model-help__intro" aria-label="Resolution explained">
          <p>Resolution is the distance between forecast points. A smaller grid can capture more local detail, but does not guarantee a better forecast.</p>
        </section>
        <table class="model-help__models" :aria-label="title">
          <thead><tr><th scope="col">Model</th><th scope="col">Resolution</th><th scope="col">Coverage</th></tr></thead>
          <tbody>
            <tr v-for="model in availableModels" :key="model.id">
              <th scope="row">{{ model.name }}</th><td>{{ model.details[0] }}</td><td>{{ model.details[1] }}</td>
            </tr>
          </tbody>
        </table>
        <details v-if="otherModels.length" class="model-help__other">
          <summary>Models for other regions</summary>
          <p>These are not offered for your current spot. Regional choices are filtered by country; model coverage can extend beyond those borders.</p>
          <table class="model-help__models">
            <thead><tr><th scope="col">Model</th><th scope="col">Resolution</th><th scope="col">Coverage</th></tr></thead>
            <tbody>
              <tr v-for="model in otherModels" :key="model.id">
                <th scope="row">{{ model.name }}</th><td>{{ model.details[0] }}</td><td>{{ model.details[1] }}</td>
              </tr>
            </tbody>
          </table>
        </details>
        <section class="model-help__intro" aria-label="How forecasts are combined">
          <template v-if="isWind">
            <p>Beyond the regional forecast. Best Match fills missing hours, including when your chosen model ends earlier.</p>
            <p>Wind speeds are in knots. In Numbers view, wind is above gusts. Arrows point where the wind is blowing.</p>
            <p>Your wind model also supplies weather and temperature. Waves use a separate model.</p>
          </template>
          <template v-else>
            <p>Missing coverage or later hours are filled automatically: Best Match and MFWAM use GFS, GFS uses its global grid, and EWAM uses global GWAM. Available data from your chosen model comes first.</p>
            <p>Heights are in metres. Arrows point where the swell is travelling; the period in seconds is the time between waves.</p>
            <p>MFWAM and GFS can show a main swell and a dashed second swell. Direction and period belong to the main swell. EWAM combines swell into one line.</p>
          </template>
        </section>
        <DialogClose as-child>
          <button class="reterminal-help__close" type="button" :aria-label="`Close ${isWind ? 'wind' : 'wave'} model help`">
            <svg viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m4 4 8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" /></svg>
          </button>
        </DialogClose>
      </DialogContent>
    </DialogPortal>
  </DialogRoot>
</template>

<style scoped>
.model-help { inline-size: min(35rem, calc(100% - 2rem)); gap: 1.25rem; }
.model-help__intro, .model-help__models, .model-help__other { font: inherit; margin: 0; }
.model-help__intro { display: grid; gap: 0.625rem; }
.model-help__intro p { margin: 0; }
.model-help th { font-weight: 400; }
.model-help thead th { font-weight: 500; }
.model-help__models { border-collapse: collapse; width: 100%; table-layout: fixed; text-align: left; }
.model-help__models th, .model-help__models td { padding: 0.625rem 0.75rem 0.625rem 0; vertical-align: top; border-bottom: 1px solid var(--settings-control-border, rgb(128 128 128 / 25%)); }
.model-help__models th:first-child { width: 6.25rem; }
.model-help__models th:nth-child(2) { width: 6rem; }
.model-help__models td:last-child, .model-help__models th:last-child { padding-inline-end: 0; }
.model-help__other summary { cursor: pointer; }
.model-help__other summary:focus-visible { outline: 2px solid var(--settings-focus); outline-offset: 4px; }
@media (max-width: 30rem) {
  .model-help__models th:first-child { width: 5.25rem; }
  .model-help__models th:nth-child(2) { width: 5rem; }
  .model-help__models th, .model-help__models td { padding-inline-end: 0.5rem; }
}
</style>
