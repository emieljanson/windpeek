<script setup>
import { computed, ref, watch } from 'vue'
import { useConfiguratorStore } from '../../stores/configurator'
import { SWELL_MODELS } from '../../forecast/openMeteoSwell'
import SettingRow from './SettingRow.vue'
import ForecastModelHelpDialog from '../ForecastModelHelpDialog.vue'
import SettingSelect from './SettingSelect.vue'
import SettingSwitch from './SettingSwitch.vue'
import SettingSegments from './SettingSegments.vue'
import SettingNumberInput from './SettingNumberInput.vue'

const props = defineProps({ installerOpen: { type: Boolean, default: false } })
const expanded = ref(false)
const modelHelpOpen = ref(false)
const modelHelpKind = ref('wind')
let modelHelpTrigger
function openModelHelp(kind, event) {
  modelHelpKind.value = kind
  modelHelpTrigger = event.currentTarget
  modelHelpOpen.value = true
}
function restoreModelHelpFocus() { modelHelpTrigger?.focus() }
watch(() => props.installerOpen, (open) => {
  if (open) expanded.value = false
})

const store = useConfiguratorStore()
const temperatureUnits = [{ value: 'celsius', label: '°C' }, { value: 'fahrenheit', label: '°F' }]
const weatherModels = computed(() => {
  const bestMatchModels = store.availableForecastModels.filter((model) => model.id === 'best_match')
  const localModels = store.availableForecastModels.filter((model) => model.availability === 'regional')
  const globalModels = store.availableForecastModels.filter((model) => (
    model.availability === 'always' && model.id !== 'best_match'
  ))

  return [
    ...bestMatchModels.map((model) => ({ value: model.id, label: model.label })),
    ...localModels.map((model, index) => ({
      value: model.id,
      label: model.label,
      separatorBefore: index === 0 && bestMatchModels.length > 0,
    })),
    ...globalModels.map((model, index) => ({
      value: model.id,
      label: model.label,
      separatorBefore: index === 0 && (bestMatchModels.length > 0 || localModels.length > 0),
    })),
  ]
})

</script>

<template>
  <details class="forecast-advanced" :open="expanded" @toggle="expanded = $event.target.open">
    <summary>
      <span>Advanced</span>
      <svg class="forecast-advanced__chevron" viewBox="0 0 16 16" fill="none" aria-hidden="true" focusable="false">
        <path fill="currentColor" d="M4.53033 5.46967C4.23744 5.17678 3.76256 5.17678 3.46967 5.46967C3.17678 5.76256 3.17678 6.23744 3.46967 6.53033L7.46967 10.5303C7.76001 10.8207 8.22986 10.8236 8.52376 10.5368L12.5238 6.63419C12.8202 6.34493 12.8261 5.87009 12.5368 5.57361C12.2476 5.27713 11.7727 5.27128 11.4762 5.56054L8.00649 8.94583L4.53033 5.46967Z" />
      </svg>
    </summary>
    <div class="forecast-advanced__rows">
      <SettingRow label="Wind model">
        <template #label-action>
          <button class="setting-row__help" type="button" aria-label="About wind models" aria-haspopup="dialog"
            :aria-expanded="modelHelpOpen && modelHelpKind === 'wind'" @click="openModelHelp('wind', $event)">Wind model</button>
        </template>
        <SettingSelect :model-value="store.selectedModelId" :options="weatherModels" name="model"
          @update:model-value="store.selectModel" />
      </SettingRow>
      <SettingRow label="Wave model">
        <template #label-action>
          <button class="setting-row__help" type="button" aria-label="About wave models" aria-haspopup="dialog"
            :aria-expanded="modelHelpOpen && modelHelpKind === 'wave'" @click="openModelHelp('wave', $event)">Wave model</button>
        </template>
        <SettingSelect :model-value="store.selectedSwellModelId" :options="SWELL_MODELS" name="swell-model"
          @update:model-value="store.setSwellModel" />
      </SettingRow>
      <SettingRow v-if="store.windSize === 'large'" label="Wind threshold">
        <SettingSwitch :model-value="store.showThreshold" name="show-threshold"
          @update:model-value="store.setShowThreshold" />
      </SettingRow>
      <SettingRow v-if="store.windSize === 'large' && store.showThreshold" label="Minimum wind" class="setting-row--compact-control">
        <SettingNumberInput :model-value="store.threshold" :min="0" :max="99" :step="1" unit="kt" name="threshold"
          @update:model-value="store.setThreshold" />
      </SettingRow>
      <SettingRow label="Temperature">
        <SettingSegments :model-value="store.temperatureUnit" :options="temperatureUnits" name="temperature-unit"
          @update:model-value="store.setTemperatureUnit" />
      </SettingRow>
      <SettingRow label="Footer">
        <SettingSwitch :model-value="store.showDedicatedFooter" name="show-dedicated-footer"
          @update:model-value="store.setShowDedicatedFooter" />
      </SettingRow>
    </div>
  </details>
  <ForecastModelHelpDialog v-model:open="modelHelpOpen" :kind="modelHelpKind" :available-wind-model-ids="store.availableForecastModels.map(model => model.id)" @closed="restoreModelHelpFocus" />
</template>

<style scoped>
.forecast-advanced {
  margin-inline: -0.75rem;
  padding-inline: 0.75rem;
  border-block: 1px solid var(--settings-divider);
}
summary {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-block-size: 2.5rem;
  padding-inline-end: 0.5rem;
  list-style: none;
  cursor: pointer;
  color: var(--settings-control-ink);
  font-size: var(--settings-label-size);
  font-weight: 500;
}
summary::-webkit-details-marker { display: none; }
summary:focus-visible { outline: 2px solid var(--settings-focus); outline-offset: 2px; }
.forecast-advanced__chevron {
  flex: none;
  inline-size: 1rem;
  block-size: 1rem;
  color: var(--settings-control-muted);
}
.forecast-advanced[open] .forecast-advanced__chevron { transform: rotate(180deg); }
.forecast-advanced__rows { display: grid; gap: 0.5rem; padding-block-end: 0.75rem; }

/* Keep the native disclosure semantics while its content resizes the panel. */
@supports selector(details::details-content) {
  .forecast-advanced { interpolate-size: allow-keywords; }
  .forecast-advanced::details-content {
    block-size: 0;
    overflow: clip;
    transition: block-size 200ms cubic-bezier(0.23, 1, 0.32, 1),
      content-visibility 200ms allow-discrete;
  }
  .forecast-advanced[open]::details-content { block-size: auto; }
}

@media (prefers-reduced-motion: reduce) {
  .forecast-advanced::details-content { transition: none; }
}
</style>
