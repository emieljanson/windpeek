<script setup>
import { computed, defineAsyncComponent, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import {
  BOARD_IDS,
  SUPPORTED_BOARD_IDS,
  createInstalledConfiguration,
  displayConfigurationFromStore,
} from '../config/configuration'
import { brouwersdamTide } from '../fixtures/brouwersdam'
import { getSerialSupport } from '../installer/serialPortAdapter'
import { deviceTimezone } from '../timezone'
import InstallContinuation from '../components/InstallContinuation.vue'
import WindScoutSettings from '../components/WindScoutSettings.vue'
import { useCompactViewport } from '../composables/useCompactViewport'
import { useConfiguratorStore } from '../stores/configurator'

const WindScoutScene = defineAsyncComponent(() => import('../components/WindScoutScene.vue'))

const store = useConfiguratorStore()
const currentDeviceTimezone = deviceTimezone()
const { isCompact } = useCompactViewport()
const routeParams = new URLSearchParams(window.location.search)
const requestedPreviewBoardId = routeParams.get('devicePreview')
const installerDemoMode = routeParams.has('installerDemo')
const previewBoardId = SUPPORTED_BOARD_IDS.includes(requestedPreviewBoardId)
  ? requestedPreviewBoardId
  : null
const captureMode = Boolean(previewBoardId)
if (captureMode) {
  store.setShowThreshold(true)
  if (previewBoardId === BOARD_IDS.E1003) {
    store.$patch({ tide: brouwersdamTide, tideStatus: 'available', showTide: true })
  }
}
const sceneFailed = ref(false)
const sceneError = ref('')
const installerOpen = ref(false)
const showUsbConnection = ref(false)
const showInstaller = computed(() => (
  !isCompact.value && getSerialSupport().reason !== 'desktop-required'
))
const installationConfiguration = computed(() => createInstalledConfiguration({
  deviceTimezone: currentDeviceTimezone,
  spot: store.spotById(store.selectedSpotId),
  modelId: store.selectedModelId,
  boardId: store.selectedBoardId,
  display: displayConfigurationFromStore(store),
}))
let visualViewportFrame

function updateVisualViewportInset() {
  visualViewportFrame = undefined
  const viewport = window.visualViewport
  const bottomInset = viewport
    ? Math.max(0, window.innerHeight - viewport.height - viewport.offsetTop)
    : 0
  document.documentElement.style.setProperty('--visual-viewport-bottom', `${bottomInset}px`)
}

function scheduleVisualViewportUpdate() {
  if (visualViewportFrame !== undefined) return
  visualViewportFrame = requestAnimationFrame(updateVisualViewportInset)
}

function handleSceneError(reason) {
  sceneFailed.value = true
  sceneError.value = reason || 'The 3D model could not be loaded.'
}

function closeInstaller() {
  installerOpen.value = false
  showUsbConnection.value = false
}

watch(isCompact, (compact) => {
  if (compact) {
    installerOpen.value = false
    showUsbConnection.value = false
  }
  document.documentElement.classList.toggle('configurator-is-compact', compact)
}, { immediate: true })

onMounted(() => {
  scheduleVisualViewportUpdate()
  window.addEventListener('resize', scheduleVisualViewportUpdate)
  window.visualViewport?.addEventListener('resize', scheduleVisualViewportUpdate)
  window.visualViewport?.addEventListener('scroll', scheduleVisualViewportUpdate)
  if (!captureMode) {
    void store.initializeForecast()
    void store.initializeTide()
    if (!installerDemoMode) void store.initializeNearbyDefault()
  }
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', scheduleVisualViewportUpdate)
  window.visualViewport?.removeEventListener('resize', scheduleVisualViewportUpdate)
  window.visualViewport?.removeEventListener('scroll', scheduleVisualViewportUpdate)
  if (visualViewportFrame !== undefined) cancelAnimationFrame(visualViewportFrame)
  document.documentElement.style.removeProperty('--visual-viewport-bottom')
  document.documentElement.classList.remove('configurator-is-compact')
})
</script>

<template>
  <div
    class="configurator-page"
    :data-nearby-default-status="store.nearbyDefaultStatus"
    :class="{
      'configurator-page--compact': isCompact && !captureMode,
      'configurator-page--device-capture': captureMode,
    }"
  >
    <main id="main-content" class="configurator-layout">
      <section class="product-stage" aria-label="Windscout 3D preview">
        <WindScoutScene
          v-if="!sceneFailed"
          :key="previewBoardId || store.selectedBoardId"
          class="device-scene"
          :board-id="previewBoardId || store.selectedBoardId"
          :capture-mode="captureMode"
          :focus-usb-connection="showUsbConnection"
          :show-usb-cable="installerOpen"
          @error="handleSceneError"
        />

        <div v-else class="scene-error" data-testid="scene-error" role="alert">
          <h2>The virtual Windscout could not start.</h2>
          <p>{{ sceneError }} Refresh the page to try again.</p>
        </div>
      </section>

      <aside
        v-if="!captureMode"
        class="settings-panel"
        :class="{ 'settings-panel--compact': isCompact }"
        aria-label="Windscout settings"
      >
        <WindScoutSettings :compact="isCompact" />
        <InstallContinuation
          v-if="showInstaller"
          :configuration="installationConfiguration"
          @open="installerOpen = true"
          @close="closeInstaller"
          @usb-step-change="showUsbConnection = $event"
        />
      </aside>
    </main>
  </div>
</template>

<style src="../styles/configurator.css"></style>
