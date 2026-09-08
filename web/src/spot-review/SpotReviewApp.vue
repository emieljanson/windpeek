<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'

import { geoapifyApiKey } from '../map/geoapify'
import { createGeoapifyMap } from '../map/geoapifyMap'
import { buildReviewQueue, createReviewDecision } from './reviewState'

const loading = ref(true)
const saving = ref(false)
const error = ref('')
const payload = ref({ candidates: [], results: [], decisions: [], duplicateGroups: [] })
const index = ref(0)
const name = ref('')
const latitude = ref(0)
const longitude = ref(0)
const rejectionReason = ref('')
const mapElement = ref(null)
const mapError = ref('')
let mapController

const queue = computed(() => buildReviewQueue(payload.value.candidates, payload.value.results, payload.value.decisions))
const current = computed(() => queue.value[index.value] ?? null)
const progress = computed(() => queue.value.length ? `${index.value + 1} of ${queue.value.length}` : 'Queue clear')
const duplicateMatches = computed(() => payload.value.duplicateGroups.filter((group) => (
  group.leftId === current.value?.id || group.rightId === current.value?.id
)))

async function load() {
  loading.value = true
  error.value = ''
  try {
    const response = await fetch('/api/spot-review/')
    if (!response.ok) throw new Error('Review data could not be loaded.')
    payload.value = await response.json()
    index.value = Math.min(index.value, Math.max(0, queue.value.length - 1))
  } catch (nextError) {
    error.value = nextError.message
  } finally {
    loading.value = false
  }
}

function syncFields(candidate) {
  mapController?.destroy()
  mapController = undefined
  mapError.value = ''
  if (!candidate) return
  name.value = candidate.name
  latitude.value = candidate.latitude
  longitude.value = candidate.longitude
  rejectionReason.value = ''
  void nextTick(createMap)
}

async function createMap() {
  if (!mapElement.value || !current.value) return
  try {
    mapController = await createGeoapifyMap(mapElement.value, {
      apiKey: geoapifyApiKey(),
      center: { latitude: Number(latitude.value), longitude: Number(longitude.value) },
      zoom: 13,
      onCenterChange(center) {
        latitude.value = Number(center.latitude.toFixed(6))
        longitude.value = Number(center.longitude.toFixed(6))
      },
    })
  } catch {
    mapError.value = 'Map tiles unavailable. You can still correct the coordinates below.'
  }
}

async function save(action) {
  if (!current.value || saving.value) return
  saving.value = true
  error.value = ''
  try {
    const previousId = current.value.previousDecision?.windpeekId
    const decision = createReviewDecision(current.value, current.value.validation, {
      action,
      name: name.value,
      latitude: Number(latitude.value),
      longitude: Number(longitude.value),
      windpeekId: previousId,
      reason: rejectionReason.value,
    })
    const response = await fetch('/api/spot-review/decision', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(decision),
    })
    if (!response.ok) throw new Error((await response.json()).error || 'Decision could not be saved.')
    await load()
  } catch (nextError) {
    error.value = nextError.message
  } finally {
    saving.value = false
  }
}

function move(direction) {
  if (!queue.value.length) return
  index.value = Math.max(0, Math.min(queue.value.length - 1, index.value + direction))
}

function keydown(event) {
  if (event.target?.matches('input, textarea')) return
  if (event.key === 'ArrowLeft') move(-1)
  if (event.key === 'ArrowRight') move(1)
}

watch(current, syncFields, { immediate: true })
onMounted(() => {
  window.addEventListener('keydown', keydown)
  void load()
})
onBeforeUnmount(() => {
  window.removeEventListener('keydown', keydown)
  mapController?.destroy()
})
</script>

<template>
  <main class="review-shell">
    <header class="review-header">
      <div>
        <p class="review-eyebrow">Windpeek data tools</p>
        <h1>Spot review</h1>
      </div>
      <span class="review-progress">{{ progress }}</span>
    </header>

    <p v-if="error" class="review-error" role="alert">{{ error }}</p>
    <section v-if="loading" class="review-empty">Loading review queue…</section>
    <section v-else-if="!current" class="review-empty">
      <h2>Queue clear</h2>
      <p>Every uncertain candidate has a current decision.</p>
    </section>
    <section v-else class="review-card">
      <div class="review-map-wrap">
        <div ref="mapElement" class="review-map" aria-label="Map showing the candidate position" />
        <div class="review-pin" aria-hidden="true"><span /></div>
        <p v-if="mapError" class="review-map-error">{{ mapError }}</p>
      </div>

      <div class="review-details">
        <div class="review-source-row">
          <span>{{ current.source }} · {{ current.featureType }}</span>
          <a :href="current.sourceRef" target="_blank" rel="noreferrer">Open source ↗</a>
        </div>
        <label>
          <span>Name</span>
          <input v-model="name" autocomplete="off" />
        </label>
        <div class="review-coordinate-grid">
          <label><span>Latitude</span><input v-model.number="latitude" inputmode="decimal" /></label>
          <label><span>Longitude</span><input v-model.number="longitude" inputmode="decimal" /></label>
        </div>
        <div class="review-facts">
          <div><span>Activities</span><strong>{{ current.activities.join(', ') }}</strong></div>
          <div><span>Review reasons</span><strong>{{ current.validation.reasons.join(', ') }}</strong></div>
          <div v-if="duplicateMatches.length"><span>Possible duplicates</span><strong>{{ duplicateMatches.length }}</strong></div>
        </div>
        <label>
          <span>Reason when rejecting</span>
          <input v-model="rejectionReason" placeholder="Forbidden, not a watersport location…" />
        </label>
        <div class="review-actions">
          <button type="button" class="button-secondary" :disabled="index === 0" @click="move(-1)">Previous</button>
          <button type="button" class="button-danger" :disabled="saving" @click="save('reject')">Reject</button>
          <button type="button" class="button-primary" :disabled="saving" @click="save('approve')">Approve spot</button>
          <button type="button" class="button-secondary" :disabled="index >= queue.length - 1" @click="move(1)">Next</button>
        </div>
      </div>
    </section>
  </main>
</template>

