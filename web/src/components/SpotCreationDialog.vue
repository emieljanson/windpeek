<script setup>
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue'
import {
  DialogContent,
  DialogDescription,
  DialogOverlay,
  DialogPortal,
  DialogRoot,
  DialogTitle,
} from 'reka-ui'
import { toast } from 'vue-sonner'
import {
  geoapifyApiKey,
  reverseGeoapifyLocation,
  searchGeoapifyPlaces,
} from '../map/geoapify'

const props = defineProps({
  open: { type: Boolean, required: true },
  initialQuery: { type: String, default: '' },
  apiKey: { type: String, default: geoapifyApiKey },
  searchPlaces: { type: Function, default: searchGeoapifyPlaces },
  reverseLocation: { type: Function, default: reverseGeoapifyLocation },
  createMap: {
    type: Function,
    default: async (...args) => {
      const { createGeoapifyMap } = await import('../map/geoapifyMap')
      return createGeoapifyMap(...args)
    },
  },
  saveSpot: { type: Function, default: null },
})

const emit = defineEmits(['update:open', 'confirm'])
const DEFAULT_CENTER = Object.freeze({ latitude: 52.2, longitude: 5.3 })
const searchTerm = ref('')
const results = ref([])
const selectedPlace = ref(null)
const activeResultIndex = ref(-1)
const searching = ref(false)
const resultsOpen = ref(false)
const searchedQuery = ref('')
const saving = ref(false)
const errorMessage = ref('')
const searchErrorMessage = ref('')
const mapErrorMessage = ref('')
const mapContainer = ref(null)
const searchInput = ref(null)
const initialSearchFocus = ref(true)
const pointerSearchFocus = ref(false)
const center = ref(null)
const mapReady = ref(false)
let debounceTimer
let searchController
let mapController
let mapGeneration = 0
let mapCreationController
let reverseController
let suppressNextSearchOpen = false
let automaticFocus = false

const activeResultId = computed(() => activeResultIndex.value >= 0
  ? `spot-search-result-${activeResultIndex.value}`
  : undefined)
const hasCompletedEmptySearch = computed(() => (
  resultsOpen.value
  && !searching.value
  && !searchErrorMessage.value
  && searchedQuery.value.length >= 2
  && results.value.length === 0
))
const showResults = computed(() => resultsOpen.value && (
  searching.value
  || results.value.length > 0
  || hasCompletedEmptySearch.value
))
const confirmLabel = computed(() => {
  if (saving.value) return 'Adding…'
  return selectedPlace.value ? `Add ${selectedPlace.value.name}` : 'Add spot'
})

function language() {
  return String(globalThis.navigator?.language ?? 'en').slice(0, 2)
}

function clearSearch() {
  clearTimeout(debounceTimer)
  debounceTimer = undefined
  searchController?.abort()
  searchController = undefined
  searching.value = false
}

function destroyMap() {
  mapGeneration += 1
  mapReady.value = false
  mapCreationController?.abort()
  mapCreationController = undefined
  mapController?.destroy()
  mapController = undefined
}

function clearReverse() {
  reverseController?.abort()
  reverseController = undefined
  saving.value = false
}

function reset() {
  clearSearch()
  clearReverse()
  destroyMap()
  results.value = []
  selectedPlace.value = null
  activeResultIndex.value = -1
  resultsOpen.value = false
  searchedQuery.value = ''
  searching.value = false
  saving.value = false
  errorMessage.value = ''
  searchErrorMessage.value = ''
  mapErrorMessage.value = ''
  initialSearchFocus.value = true
  pointerSearchFocus.value = false
  center.value = null
}

async function runSearch(query) {
  clearSearch()
  if (!props.open || query.trim().length < 2) {
    results.value = []
    searchedQuery.value = ''
    return
  }
  const controller = new AbortController()
  searchController = controller
  searching.value = true
  searchErrorMessage.value = ''
  try {
    const nextResults = await props.searchPlaces(query, {
      apiKey: props.apiKey,
      bias: center.value,
      signal: controller.signal,
      language: language(),
    })
    if (searchController === controller && props.open) {
      results.value = nextResults
      searchedQuery.value = query.trim()
      activeResultIndex.value = -1
      if (nextResults[0]) {
        previewPlace(nextResults[0])
        searchTerm.value = nextResults[0].name
        resultsOpen.value = false
      } else {
        resultsOpen.value = true
      }
    }
  } catch (error) {
    if (searchController === controller && error?.name !== 'AbortError') {
      results.value = []
      searchErrorMessage.value = error?.message || 'Location search is temporarily unavailable.'
      toast.error(searchErrorMessage.value, { id: 'spot-search-error' })
    }
  } finally {
    if (searchController === controller) {
      searchController = undefined
      searching.value = false
    }
  }
}

function scheduleSearch(query) {
  clearSearch()
  results.value = []
  searchedQuery.value = ''
  activeResultIndex.value = -1
  debounceTimer = setTimeout(() => void runSearch(query), 300)
}

function previewPlace(place) {
  if (!place) return
  clearReverse()
  selectedPlace.value = place
  center.value = { latitude: place.latitude, longitude: place.longitude }
  errorMessage.value = ''
  searchErrorMessage.value = ''
  mapController?.setCenter?.(center.value, { zoom: 13 })
}

async function createPositioningMap() {
  if (!mapContainer.value || mapController || mapCreationController) return
  destroyMap()
  const generation = mapGeneration
  const creationController = new AbortController()
  mapCreationController = creationController
  try {
    const controller = await props.createMap(mapContainer.value, {
      apiKey: props.apiKey,
      center: center.value,
      zoom: selectedPlace.value ? 13 : 6,
      signal: creationController.signal,
      onCenterChange: (nextCenter) => {
        // MapLibre can report its initial camera position while the map is
        // still loading. At that point the place search may already have
        // selected a more precise location. Ignore those startup events so
        // they cannot replace the selected place just before confirmation.
        if (generation === mapGeneration && mapReady.value) center.value = nextCenter
      },
    })
    if (!props.open || generation !== mapGeneration) {
      controller.destroy()
      return
    }
    if (mapCreationController === creationController) mapCreationController = undefined
    mapController = controller
    mapReady.value = true
    mapErrorMessage.value = ''
    if (selectedPlace.value) {
      const selectedCenter = {
        latitude: selectedPlace.value.latitude,
        longitude: selectedPlace.value.longitude,
      }
      // Keep the confirmation coordinates correct immediately; the animated
      // map move reports its final center only after it has finished.
      center.value = selectedCenter
      controller.setCenter?.(selectedCenter, { zoom: 13 })
    }
  } catch (error) {
    if (mapCreationController === creationController) mapCreationController = undefined
    if (error?.name !== 'AbortError' && props.open && generation === mapGeneration) {
      mapErrorMessage.value = error?.message || 'The map could not be loaded.'
      toast.error(mapErrorMessage.value, { id: 'spot-map-error' })
    }
  }
}

async function choosePlace(place, event) {
  if (!place) return
  previewPlace(place)
  searchTerm.value = place.name
  clearSearch()
  resultsOpen.value = false
  activeResultIndex.value = -1
  await nextTick()
  pointerSearchFocus.value = event?.detail > 0
  const inputElement = searchInput.value
  if (inputElement && document.activeElement !== inputElement) {
    suppressNextSearchOpen = true
    inputElement.focus()
  }
}

function moveActiveResult(direction) {
  if (!results.value.length) return
  resultsOpen.value = true
  activeResultIndex.value = activeResultIndex.value < 0
    ? (direction > 0 ? 0 : results.value.length - 1)
    : (activeResultIndex.value + direction + results.value.length) % results.value.length
  document.getElementById(`spot-search-result-${activeResultIndex.value}`)?.scrollIntoView({ block: 'nearest' })
}

function chooseActiveResult() {
  const place = results.value[activeResultIndex.value] ?? results.value[0]
  if (place) void choosePlace(place)
}

function handleSearchFocus(event) {
  if (automaticFocus) return
  if (suppressNextSearchOpen) {
    suppressNextSearchOpen = false
    return
  }
  resultsOpen.value = true
  if (selectedPlace.value && searchTerm.value === selectedPlace.value.name) {
    const input = event.currentTarget
    nextTick(() => input?.select())
  }
}

function handleSearchPointerDown() {
  pointerSearchFocus.value = true
}

function handleSearchInput() {
  results.value = []
  searchedQuery.value = ''
  activeResultIndex.value = -1
  resultsOpen.value = true
}

function handleSearchClick(event) {
  resultsOpen.value = true
  if (selectedPlace.value) event.currentTarget?.select()
}

function handleSearchBlur() {
  initialSearchFocus.value = false
  pointerSearchFocus.value = false
  setTimeout(() => {
    if (document.activeElement?.closest('.spot-dialog__search')) return
    resultsOpen.value = false
    activeResultIndex.value = -1
    if (selectedPlace.value) searchTerm.value = selectedPlace.value.name
  }, 0)
}

async function confirmSpot() {
  if (!selectedPlace.value || !center.value || !mapReady.value || saving.value) return
  saving.value = true
  errorMessage.value = ''
  const controller = new AbortController()
  reverseController = controller
  const confirmedPlace = selectedPlace.value
  const confirmedCenter = { ...center.value }
  try {
    const finalLocation = await props.reverseLocation(confirmedCenter, {
      apiKey: props.apiKey,
      language: language(),
      signal: controller.signal,
    })
    if (reverseController !== controller || !props.open) return
    const timezone = finalLocation?.timezone
    if (!timezone) throw new Error('We could not determine the timezone for this pin.')
    const input = {
      name: confirmedPlace.name,
      latitude: confirmedCenter.latitude,
      longitude: confirmedCenter.longitude,
      timezone,
      countryCode: finalLocation.countryCode,
      providerRef: `geoapify:${confirmedPlace.id}`,
    }
    if (props.saveSpot && !await props.saveSpot(input)) {
      throw new Error('This spot could not be saved in this browser.')
    }
    emit('confirm', input)
    emit('update:open', false)
  } catch (error) {
    if (reverseController === controller && error?.name !== 'AbortError') {
      errorMessage.value = error?.message || 'This spot could not be added.'
      toast.error(errorMessage.value, { id: 'spot-save-error' })
    }
  } finally {
    if (reverseController === controller) {
      reverseController = undefined
      saving.value = false
    }
  }
}

function setOpen(value) {
  if (!value) reset()
  emit('update:open', value)
}

function focusSearch(event) {
  event.preventDefault()
  nextTick(() => {
    automaticFocus = true
    searchInput.value?.focus()
    automaticFocus = false
  })
}

watch(() => props.open, (open) => {
  reset()
  if (!open) return
  searchTerm.value = props.initialQuery.trim()
  center.value = { ...DEFAULT_CENTER }
  resultsOpen.value = false
  scheduleSearch(searchTerm.value)
}, { immediate: true })

watch(mapContainer, (element) => {
  if (props.open && element) void createPositioningMap()
})

watch(searchTerm, (query) => {
  if (!props.open) return
  if (selectedPlace.value && query === selectedPlace.value.name) return
  if (selectedPlace.value) {
    clearReverse()
    selectedPlace.value = null
  }
  scheduleSearch(query)
})

onBeforeUnmount(reset)
</script>

<template>
  <DialogRoot :open="props.open" @update:open="setOpen">
    <DialogPortal>
      <DialogOverlay class="spot-dialog__overlay" />
      <DialogContent class="spot-dialog" @open-auto-focus="focusSearch">
        <header class="spot-dialog__header">
          <div>
            <DialogTitle class="spot-dialog__title">Add spot</DialogTitle>
            <DialogDescription class="spot-dialog__description">
              Move the map until the pin is on your spot by the water
            </DialogDescription>
          </div>
          <button class="spot-dialog__close" type="button" aria-label="Close spot dialog" @click="setOpen(false)">
            <svg viewBox="0 0 16 16" fill="none" aria-hidden="true">
              <path fill="currentColor" d="M3.96967 3.96967C4.26256 3.67678 4.73744 3.67678 5.03033 3.96967L8 6.939L10.9697 3.96967C11.2626 3.67678 11.7374 3.67678 12.0303 3.96967C12.3232 4.26256 12.3232 4.73744 12.0303 5.03033L9.061 8L12.0303 10.9697C12.2966 11.2359 12.3208 11.6526 12.1029 11.9462L12.0303 12.0303C11.7374 12.3232 11.2626 12.3232 10.9697 12.0303L8 9.061L5.03033 12.0303C4.73744 12.3232 4.26256 12.3232 3.96967 12.0303C3.67678 11.7374 3.67678 11.2626 3.96967 10.9697L6.939 8L3.96967 5.03033C3.7034 4.76406 3.6792 4.3474 3.89705 4.05379L3.96967 3.96967Z" />
            </svg>
          </button>
        </header>

        <div class="spot-dialog__workspace">
          <div class="spot-dialog__search">
            <div class="spot-dialog__search-field">
              <svg class="spot-dialog__search-icon" viewBox="0 0 16 16" fill="none" aria-hidden="true">
                <path fill="currentColor" fill-rule="evenodd" clip-rule="evenodd" d="M7 1.99805C9.76142 1.99805 12 4.23662 12 6.99805C12 8.10816 11.6375 9.13324 11.0254 9.96289L13.7803 12.7178L13.832 12.7744C14.0723 13.069 14.0549 13.5037 13.7803 13.7783C13.5057 14.0529 13.0709 14.0704 12.7764 13.8301L12.7197 13.7783L9.96484 11.0234C9.13519 11.6355 8.11012 11.998 7 11.998C4.23858 11.998 2 9.75947 2 6.99805C2 4.23662 4.23858 1.99805 7 1.99805ZM7 3.49805C5.067 3.49805 3.5 5.06505 3.5 6.99805C3.5 8.93104 5.067 10.498 7 10.498C8.933 10.498 10.5 8.93104 10.5 6.99805C10.5 5.06505 8.933 3.49805 7 3.49805Z" />
              </svg>
              <input
                ref="searchInput"
                v-model="searchTerm"
                class="spot-dialog__search-input"
                :class="{
                  'is-initial-focus': initialSearchFocus,
                  'is-pointer-focus': pointerSearchFocus,
                }"
                role="combobox"
                aria-label="Search for a place"
                aria-autocomplete="list"
                :aria-expanded="showResults ? 'true' : 'false'"
                aria-controls="spot-search-results"
                :aria-activedescendant="activeResultId"
                :aria-busy="searching ? 'true' : undefined"
                placeholder="Search for a place"
                @keydown.down.prevent="moveActiveResult(1)"
                @keydown.up.prevent="moveActiveResult(-1)"
                @keydown.enter.prevent="chooseActiveResult"
                @input="handleSearchInput"
                @pointerdown="handleSearchPointerDown"
                @click="handleSearchClick"
                @focus="handleSearchFocus"
                @keydown="initialSearchFocus = false; pointerSearchFocus = false"
                @blur="handleSearchBlur"
              >
            </div>
            <div
              v-if="showResults"
              id="spot-search-results"
              class="spot-dialog__results"
              role="listbox"
              aria-label="Place results"
            >
              <p v-if="searching" class="spot-dialog__status" role="status">Searching places…</p>
              <template v-else>
                <button
                  v-for="(place, index) in results"
                  :id="`spot-search-result-${index}`"
                  :key="place.id"
                  class="spot-dialog__result"
                  :class="{ 'is-active': activeResultIndex === index }"
                  type="button"
                  role="option"
                  aria-selected="false"
                  @pointermove="activeResultIndex = index"
                  @click="choosePlace(place, $event)"
                >
                  <span class="spot-dialog__result-name">{{ place.name }}</span>
                  <span class="spot-dialog__result-description">{{ place.description }}</span>
                </button>
              </template>
              <p v-if="hasCompletedEmptySearch" class="spot-dialog__status" role="status">
                No location found
              </p>
            </div>
          </div>

          <div class="spot-dialog__map-wrap">
            <div ref="mapContainer" class="spot-dialog__map" role="region" aria-label="Map for positioning the spot" />
            <div class="spot-dialog__pin" aria-hidden="true">
              <svg viewBox="0 0 21 34" fill="none">
                <path fill="black" fill-rule="evenodd" clip-rule="evenodd" d="M10.5 0C16.299 0 21 4.70101 21 10.5C21 15.9617 16.8299 20.4494 11.5 20.953V32C11.5 32.5523 11.0523 33 10.5 33C9.9477 33 9.5 32.5523 9.5 32V20.953C4.17007 20.4494 0 15.9617 0 10.5C0 4.70101 4.70101 0 10.5 0Z" />
                <path fill="white" d="M14 10.5C14 8.567 12.433 7 10.5 7C8.567 7 7 8.567 7 10.5C7 12.433 8.567 14 10.5 14C12.433 14 14 12.433 14 10.5Z" />
                <path fill="black" opacity=".3" d="M10.5 34C11.8807 34 13 33.4403 13 32.75C13 32.0597 11.8807 31.5 10.5 31.5C9.1193 31.5 8 32.0597 8 32.75C8 33.4403 9.1193 34 10.5 34Z" />
              </svg>
            </div>
          </div>
        </div>

        <footer class="spot-dialog__footer">
          <button
            class="spot-dialog__confirm"
            type="button"
            :disabled="saving || !mapReady || !selectedPlace"
            @click="confirmSpot"
          >
            {{ confirmLabel }}
          </button>
        </footer>
      </DialogContent>
    </DialogPortal>
  </DialogRoot>
</template>
