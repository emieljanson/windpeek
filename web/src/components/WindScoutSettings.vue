<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { useConfiguratorStore } from '../stores/configurator'
import { FLOATING_INSPECTOR_VIEWPORT_QUERY } from '../composables/useCompactViewport'
import { MAX_THRESHOLD, MIN_THRESHOLD } from '../renderer/contract'
import { DEVICE_OPTIONS } from '../config/configuration'
import { normalizeSpotQuery, searchSpots } from '../spots/searchSpots'
import ReTerminalHelpDialog from './ReTerminalHelpDialog.vue'
import SpotCreationDialog from './SpotCreationDialog.vue'
import SettingCombobox from './settings/SettingCombobox.vue'
import SettingNumberInput from './settings/SettingNumberInput.vue'
import SettingRow from './settings/SettingRow.vue'
import SettingSelect from './settings/SettingSelect.vue'
import SettingSwitch from './settings/SettingSwitch.vue'

const props = defineProps({
  compact: { type: Boolean, default: false },
})

const store = useConfiguratorStore()
const {
  effectiveShowTide,
  availableForecastModels,
  forecastLabel,
  forecastMessage,
  forecastStatus,
  selectedModelId,
  selectedBoardId,
  selectedSpotId,
  showDedicatedFooter,
  spots,
  showThreshold,
  showWeather,
  temperatureChoice,
  threshold,
  tideAvailable,
  tideMessage,
} = storeToRefs(store)

const spotSearchTerm = ref('')
const spotHasUserSelection = ref(false)
const spotSearch = ref(null)
const spotDialogOpen = ref(false)
const reTerminalHelpOpen = ref(false)
const customSpotQuery = ref('')
const filteredSpots = computed(() => (
  spotSearchTerm.value.trim().length < 2
    ? []
    : searchSpots(spots.value, spotSearchTerm.value)
))
const createSpotActionLabel = computed(() => {
  if (props.compact) return ''
  const query = spotSearchTerm.value.trim()
  if (query.length < 2) return ''
  const exactMatch = spots.value.some((spot) =>
    normalizeSpotQuery(spot.name) === normalizeSpotQuery(query))
  return exactMatch ? '' : `Add ${query}`
})

const modelOptions = computed(() => {
  const bestMatchModels = availableForecastModels.value.filter((model) => model.id === 'best_match')
  const localModels = availableForecastModels.value.filter((model) => model.availability === 'regional')
  const globalModels = availableForecastModels.value.filter((model) => (
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
const temperatureOptions = [
  { value: 'hide', label: 'Hide' },
  { value: 'celsius', label: 'Celsius' },
  { value: 'fahrenheit', label: 'Fahrenheit' },
]

watch(selectedSpotId, (spotId) => {
  if (spotHasUserSelection.value) {
    spotSearchTerm.value = store.spotById(spotId)?.name ?? ''
  }
})

function restoreCompactSpotSearch() {
  spotSearchTerm.value = spotHasUserSelection.value
    ? store.spotById(selectedSpotId.value)?.name ?? ''
    : ''
}

function selectSpot(spotId) {
  store.markUserSpotIntent()
  const spot = store.spotById(spotId)
  if (!spot) return
  spotHasUserSelection.value = true
  if (spotId !== selectedSpotId.value) void store.selectSpot(spotId)
}

onMounted(() => {
  const hasFloatingInspector = window.matchMedia?.(FLOATING_INSPECTOR_VIEWPORT_QUERY).matches
    ?? window.innerWidth > 896
  if (!props.compact && hasFloatingInspector) spotSearch.value?.focus()
})

function handleSpotDismiss() {
  if (!props.compact) return
  restoreCompactSpotSearch()
}

function createSpot(query) {
  store.markUserSpotIntent()
  customSpotQuery.value = query.trim()
  spotDialogOpen.value = true
}

function saveSpot(input) {
  store.markUserSpotIntent()
  const spot = store.addPersonalSpot(input)
  if (!spot) return null
  spotHasUserSelection.value = true
  spotSearchTerm.value = spot.name
  // The spot is already persisted. Close the dialog immediately and let the
  // forecast update in the background instead of making confirmation depend
  // on network and rendering speed.
  void store.selectSpot(spot.id)
  return spot
}

function selectModel(modelId) {
  if (modelId !== selectedModelId.value) void store.selectModel(modelId)
}

function toggleTemperature() {
  store.setTemperatureChoice(temperatureChoice.value === 'hide' ? 'celsius' : 'hide')
}

function markPillPointerFocus(event) {
  event.currentTarget.classList.add('is-pointer-focus')
}

function clearPillPointerFocus(event) {
  event.currentTarget.classList.remove('is-pointer-focus')
}
</script>

<template>
  <div class="settings-shell" :class="{ 'settings-shell--compact': props.compact }">
    <div
      class="forecast-status"
      :class="[`is-${forecastStatus}`, 'is-visually-hidden']"
      role="status"
      aria-live="polite"
      aria-atomic="true"
    >
      <span
        v-if="forecastLabel"
        class="forecast-status__label"
        data-testid="forecast-label"
      >
        {{ forecastLabel }}
      </span>
      <span class="forecast-status__message">{{ forecastMessage }}</span>
    </div>

    <div v-if="compact" class="mobile-display-pills" role="group" aria-label="Show on Windscout">
      <button
        class="mobile-display-pill"
        type="button"
        :aria-pressed="showThreshold"
        @pointerdown="markPillPointerFocus"
        @keydown="clearPillPointerFocus"
        @blur="clearPillPointerFocus"
        @click="store.setShowThreshold(!showThreshold)"
      >
        Threshold
      </button>
      <button
        class="mobile-display-pill"
        type="button"
        :aria-pressed="showWeather"
        @pointerdown="markPillPointerFocus"
        @keydown="clearPillPointerFocus"
        @blur="clearPillPointerFocus"
        @click="store.setShowWeather(!showWeather)"
      >
        Weather
      </button>
      <button
        class="mobile-display-pill"
        type="button"
        :aria-pressed="temperatureChoice !== 'hide'"
        @pointerdown="markPillPointerFocus"
        @keydown="clearPillPointerFocus"
        @blur="clearPillPointerFocus"
        @click="toggleTemperature"
      >
        Temp
      </button>
      <button
        class="mobile-display-pill"
        type="button"
        :aria-pressed="effectiveShowTide"
        :disabled="!tideAvailable"
        :title="tideAvailable ? undefined : tideMessage"
        @pointerdown="markPillPointerFocus"
        @keydown="clearPillPointerFocus"
        @blur="clearPillPointerFocus"
        @click="store.setShowTide(!effectiveShowTide)"
      >
        Tide
      </button>
    </div>

    <div v-if="!compact" class="inspector-search">
      <SettingCombobox
        ref="spotSearch"
        :model-value="selectedSpotId"
        v-model:search-term="spotSearchTerm"
        :options="filteredSpots"
        :get-option-value="(spot) => spot.id"
        :get-option-label="(spot) => spot.name"
        :create-action-label="createSpotActionLabel"
        :min-search-length="2"
        keep-selection-label
        :restore-search-on-close="spotHasUserSelection"
        :display-value="spotHasUserSelection ? undefined : () => ''"
        :open-on-focus="false"
        select-all-on-focus
        suppress-initial-focus-ring
        show-search-icon
        :inline-results="false"
        :blur-after-select="compact"
        :blur-after-dismiss="compact"
        :input-type="compact ? 'search' : 'text'"
        :input-mode="compact ? 'search' : undefined"
        :show-selection-indicator="false"
        placeholder="Search spot…"
        :empty-text="compact ? 'New spots can be created on desktop.' : 'No existing spots found'"
        name="spot"
        aria-label="Search spot"
        @update:model-value="selectSpot"
        @search-intent="store.markUserSpotIntent"
        @create="createSpot"
        @dismiss="handleSpotDismiss"
      />
    </div>

    <div v-if="!compact" class="inspector-divider" aria-hidden="true" />

    <div v-if="!compact" class="settings-surface">
      <div class="inspector-rows">
        <SettingRow label="reTerminal">
          <template #label-action>
            <button
              class="setting-row__help"
              type="button"
              aria-label="About reTerminal devices"
              aria-haspopup="dialog"
              :aria-expanded="reTerminalHelpOpen"
              @click="reTerminalHelpOpen = true"
            >
              reTerminal
            </button>
          </template>
          <SettingSelect
            :model-value="selectedBoardId"
            :options="DEVICE_OPTIONS"
            name="device"
            @update:model-value="store.setSelectedBoardId"
          />
        </SettingRow>

        <SettingRow v-if="!compact" label="Wind model">
          <SettingSelect
            :model-value="selectedModelId"
            :options="modelOptions"
            :native="compact"
            name="model"
            @update:model-value="selectModel"
          />
        </SettingRow>

        <SettingRow label="Wind threshold">
          <SettingSwitch
            :model-value="showThreshold"
            name="show-threshold"
            @update:model-value="store.setShowThreshold"
          />
        </SettingRow>

        <SettingRow v-if="showThreshold" class="setting-row--compact-control" label="Minimum wind">
          <SettingNumberInput
            :model-value="threshold"
            :min="MIN_THRESHOLD"
            :max="MAX_THRESHOLD"
            :step="1"
            unit="kt"
            name="threshold"
            @update:model-value="store.setThreshold"
          />
        </SettingRow>

        <SettingRow label="Weather">
          <SettingSwitch
            :model-value="showWeather"
            name="show-weather"
            @update:model-value="store.setShowWeather"
          />
        </SettingRow>

        <SettingRow label="Temperature">
          <SettingSelect
            :model-value="temperatureChoice"
            :options="temperatureOptions"
            :native="compact"
            :muted="temperatureChoice === 'hide'"
            name="temperature"
            @update:model-value="store.setTemperatureChoice"
          />
        </SettingRow>

        <SettingRow label="Tide">
          <SettingSwitch
            :model-value="effectiveShowTide"
            :disabled="!tideAvailable"
            :disabled-reason="tideAvailable ? '' : tideMessage"
            name="show-tide"
            @update:model-value="store.setShowTide"
          />
        </SettingRow>

        <SettingRow label="Legend">
          <SettingSwitch
            :model-value="showDedicatedFooter"
            name="show-dedicated-footer"
            @update:model-value="store.setShowDedicatedFooter"
          />
        </SettingRow>
      </div>
    </div>

    <SpotCreationDialog
      v-if="!compact"
      v-model:open="spotDialogOpen"
      :initial-query="customSpotQuery"
      :save-spot="saveSpot"
    />
    <ReTerminalHelpDialog v-model:open="reTerminalHelpOpen" />
  </div>
</template>
