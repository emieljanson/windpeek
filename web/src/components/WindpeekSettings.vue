<script setup>
import { computed, nextTick, onMounted, ref, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { toast } from 'vue-sonner'
import { useConfiguratorStore } from '../stores/configurator'
import { FLOATING_INSPECTOR_VIEWPORT_QUERY } from '../composables/useCompactViewport'
import { DEVICE_OPTIONS } from '../config/configuration'
import { normalizeSpotQuery, searchSpots } from '../spots/searchSpots'
import ReTerminalHelpDialog from './ReTerminalHelpDialog.vue'
import SpotCreationDialog from './SpotCreationDialog.vue'
import SettingCombobox from './settings/SettingCombobox.vue'
import SettingRow from './settings/SettingRow.vue'
import SettingSelect from './settings/SettingSelect.vue'
import ForecastModules from './settings/ForecastModules.vue'
import ForecastAdvanced from './settings/ForecastAdvanced.vue'

const props = defineProps({
  compact: { type: Boolean, default: false },
  installerOpen: { type: Boolean, default: false },
})

const store = useConfiguratorStore()
const {
  forecastLabel,
  forecastMessage,
  forecastStatus,
  configuredSpots,
  supportsMultipleSpots,
  selectedBoardId,
  selectedSpotId,
  spots,
} = storeToRefs(store)

const addingSpot = computed(() => supportsMultipleSpots.value && configuredSpots.value.length > 0)
const showSpotList = addingSpot
const spotSearchTerm = ref('')
const spotHasUserSelection = ref(false)
const spotSearch = ref(null)
const spotDialogOpen = ref(false)
const reTerminalHelpOpen = ref(false)
const customSpotQuery = ref('')
const filteredSpots = computed(() => (
  spotSearchTerm.value.trim().length < 2
    ? []
    : searchSpots(spots.value, spotSearchTerm.value).filter(spot => !supportsMultipleSpots.value || (!addingSpot.value && spot.id === selectedSpotId.value) || !store.configuredSpotIds.includes(spot.id))
))
const createSpotActionLabel = computed(() => {
  if (props.compact) return ''
  const query = spotSearchTerm.value.trim()
  if (query.length < 2) return ''
  const exactMatch = spots.value.some((spot) =>
    normalizeSpotQuery(spot.name) === normalizeSpotQuery(query))
  return exactMatch ? '' : `Add ${query}`
})

watch(selectedSpotId, (spotId) => {
  if (spotHasUserSelection.value && !addingSpot.value) {
    spotSearchTerm.value = addingSpot.value ? '' : store.spotById(spotId)?.name ?? ''
  }
})

function restoreCompactSpotSearch() {
  spotSearchTerm.value = spotHasUserSelection.value
    ? store.spotById(selectedSpotId.value)?.name ?? ''
    : ''
}

watch(selectedBoardId, () => {
  spotSearchTerm.value = ''
})

async function addMoreSpots() {
  if (!store.supportsMultipleSpots || store.configuredSpotIds.length >= 10) return
  store.markUserSpotIntent()
  if (!store.configuredSpotIds.length) void store.addConfiguredSpot(selectedSpotId.value)
  spotSearchTerm.value = ''
  await nextTick()
  spotSearch.value?.focus()
}

function activateSpot(spotId) {
  spotHasUserSelection.value = true
  store.markUserSpotIntent()
  void store.selectSpot(spotId)
  spotSearchTerm.value = addingSpot.value ? '' : store.spotById(spotId)?.name ?? ''
}

function notifySpotLimit() {
  if (!addingSpot.value || configuredSpots.value.length < 10) return false
  toast("Can't add more spots", { id: 'spot-limit' })
  return true
}

function commitSpot(spotId) {
  if (notifySpotLimit()) return
  if (supportsMultipleSpots.value && addingSpot.value) {
    void store.addConfiguredSpot(spotId)
  } else if (spotId !== selectedSpotId.value) void store.selectSpot(spotId)
  spotHasUserSelection.value = true
  spotSearchTerm.value = addingSpot.value ? '' : store.spotById(spotId)?.name ?? ''
}

function selectSpot(spotId) {
  store.markUserSpotIntent()
  if (!store.spotById(spotId)) return
  commitSpot(spotId)
}

onMounted(() => {
  const hasFloatingInspector = window.matchMedia?.(FLOATING_INSPECTOR_VIEWPORT_QUERY).matches
    ?? window.innerWidth > 896
  if (!props.compact && hasFloatingInspector) spotSearch.value?.focus()
})

function handleSpotDismiss() {
  if (addingSpot.value) {
    spotSearchTerm.value = ''
    return
  }
  if (!props.compact) return
  restoreCompactSpotSearch()
}

function createSpot(query) {
  if (notifySpotLimit()) return
  store.markUserSpotIntent()
  customSpotQuery.value = query.trim()
  spotDialogOpen.value = true
}

function saveSpot(input) {
  if (notifySpotLimit()) return null
  store.markUserSpotIntent()
  const spot = store.addPersonalSpot(input)
  if (!spot) return null
  spotHasUserSelection.value = true
  spotSearchTerm.value = addingSpot.value ? '' : spot.name
  // The spot is already persisted. Close the dialog immediately and let the
  // forecast update in the background instead of making confirmation depend
  // on network and rendering speed.
  commitSpot(spot.id)
  spotSearchTerm.value = addingSpot.value ? '' : spot.name
  return spot
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


    <div v-if="!compact || addingSpot" class="inspector-search" :class="{ 'inspector-search--adding': addingSpot }">
      <SettingCombobox
        ref="spotSearch"
        :model-value="addingSpot ? undefined : selectedSpotId"
        v-model:search-term="spotSearchTerm"
        :options="filteredSpots"
        :get-option-value="(spot) => spot.id"
        :get-option-label="(spot) => spot.name"
        :create-action-label="createSpotActionLabel"
        :min-search-length="2"
        :keep-selection-label="!addingSpot"
        :restore-search-on-close="!addingSpot && spotHasUserSelection"
        :display-value="!addingSpot && spotHasUserSelection ? undefined : () => ''"
        :open-on-focus="false"
        select-all-on-focus
        suppress-initial-focus-ring
        show-search-icon
        :inline-results="false"
        :blur-after-select="compact || addingSpot"
        :blur-after-dismiss="compact"
        :input-type="compact ? 'search' : 'text'"
        :input-mode="compact ? 'search' : undefined"
        :show-selection-indicator="false"
        :placeholder="addingSpot ? 'Add spot…' : 'Search spot…'"
        :empty-text="compact ? 'New spots can be created on desktop.' : 'No existing spots found'"
        name="spot"
        :aria-label="addingSpot ? 'Add spot' : 'Search spot'"
        @update:model-value="selectSpot"
        @search-intent="store.markUserSpotIntent"
        @focus="notifySpotLimit"
        @create="createSpot"
        @dismiss="handleSpotDismiss"
      />
    </div>

    <div v-if="showSpotList" class="inspector-divider" aria-hidden="true" />
    <section v-if="showSpotList" class="spot-list" aria-label="Spots">
      <div v-for="spot in configuredSpots" :key="spot.id" class="spot-list__row" :class="{ 'is-selected': selectedSpotId === spot.id }">
        <button type="button" class="spot-list__select" :aria-pressed="selectedSpotId === spot.id" @click="activateSpot(spot.id)">
          <span class="spot-list__name">{{ spot.name }}</span>
        </button>
        <button type="button" class="spot-list__remove" :aria-label="`Remove ${spot.name}`" @click="store.removeConfiguredSpot(spot.id)">
          <svg viewBox="0 0 16 16" fill="none" aria-hidden="true" focusable="false">
            <path d="m4.5 4.5 7 7m0-7-7 7" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" />
          </svg>
        </button>
      </div>
    </section>
    <ForecastModules v-if="compact" />
    <ForecastAdvanced v-if="compact" :installer-open="props.installerOpen" />

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

        <ForecastModules />

        <ForecastAdvanced :installer-open="props.installerOpen" />
      </div>
    </div>

    <button
      v-if="!compact && supportsMultipleSpots && !addingSpot && !props.installerOpen"
      type="button"
      class="setting-control add-more-spots"
      :disabled="configuredSpots.length >= 10 || addingSpot"
      @click="addMoreSpots"
    >{{ configuredSpots.length >= 10 ? 'Maximum of 10 spots' : 'Add more spots' }}</button>

    <SpotCreationDialog
      v-if="!compact"
      v-model:open="spotDialogOpen"
      :initial-query="customSpotQuery"
      :save-spot="saveSpot"
    />
    <ReTerminalHelpDialog v-model:open="reTerminalHelpOpen" />
  </div>
</template>
