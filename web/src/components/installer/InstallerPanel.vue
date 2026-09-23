<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { createInstallerSession } from '../../installer/createInstallerSession'
import { getSerialSupport, installerTransports } from '../../installer/serialPortAdapter'
import { BOARD_IDS } from '../../config/configuration'
import ReTerminalHelpDialog from '../ReTerminalHelpDialog.vue'
import InstallerComplete from './InstallerComplete.vue'
import InstallerConnect from './InstallerConnect.vue'
import InstallerDiagnosticStatus from './InstallerDiagnosticStatus.vue'
import InstallerProgress from './InstallerProgress.vue'
import InstallerStateIcon from './InstallerStateIcon.vue'
import InstallerWifi from './InstallerWifi.vue'

const props = defineProps({
  configuration: { type: Object, required: true },
  sessionFactory: { type: Function, default: createInstallerSession },
})
const emit = defineEmits(['close', 'installer-phase-change', 'usb-step-change'])
const root = ref(null)
const activeView = ref(null)
const state = ref({ phase: 'ready', progress: 0, safeToDisconnect: true, error: null })
const networks = ref([])
const wifiBusy = ref(false)
const scanBusy = ref(false)
const wifiReady = ref(false)
const reTerminalHelpOpen = ref(false)
let scanPromise = null
const support = getSerialSupport()
const transports = installerTransports(globalThis.navigator, props.configuration.boardId)
const activeTransport = ref(transports[0])
const chooserCancelled = ref(false)
const fallbackTransport = computed(() => transports.find(transport => transport !== activeTransport.value) ?? '')
const session = props.sessionFactory({
  configuration: props.configuration,
})
const isDemo = session.isDemo === true
const deviceLabel = computed(() => ({
  [BOARD_IDS.E1001]: 'reTerminal E1001',
  [BOARD_IDS.E1002]: 'reTerminal E1002',
  [BOARD_IDS.E1003]: 'reTerminal E1003',
})[props.configuration.boardId || BOARD_IDS.E1002] ?? 'supported reTerminal')
const unsupportedReason = computed(() => (support.supported || isDemo) ? '' : 'Update to a current desktop version of Firefox, Chrome, or Edge to install Windpeek over USB.')
const displayPhase = computed(() => {
  if (unsupportedReason.value && state.value.phase === 'ready') return 'error'
  if (state.value.phase === 'wifi' && !wifiReady.value) return 'wifi-scanning'
  return state.value.phase
})
const unsubscribe = session.subscribe((next) => { state.value = { ...next } })

watch(
  () => state.value.phase,
  (phase) => {
    emit('usb-step-change', phase === 'ready')
    emit('installer-phase-change', phase)
  },
  { immediate: true },
)

const critical = computed(() => !state.value.safeToDisconnect)
const progressCopy = computed(() => ({
  ready: ['Install Windpeek', `Ready to connect a ${deviceLabel.value}.`],
  'checking-device': ['Checking device', 'Keep the USB cable connected.'],
  downloading: ['Preparing firmware', 'Downloading the latest firmware.'],
  'installing-firmware': ['Writing firmware', 'Keep the USB cable connected until writing is complete.'],
  reconnecting: ['Finding Windpeek', 'Waiting for the device to restart over USB.'],
  'wifi-scanning': ['Finding Wi-Fi networks', 'Windpeek is checking which networks are nearby.'],
  configuring: ['Applying setup', 'Your spot and display options are being transferred.'],
  error: ['Setup interrupted', ''],
  reconnect: ['Reconnect your device', ''],
  'verification-issue': ['Setup didn’t finish', ''],
  wifi: ['Connect to Wi-Fi', ''],
  verifying: ['Checking the forecast', 'Keep the USB cable connected.'],
}[displayPhase.value] ?? ['Working…', 'Windpeek is continuing setup.']))

async function focusStep() {
  await nextTick()
  const target = activeView.value?.querySelector('[data-autofocus]') ??
    activeView.value?.querySelector('button, input, select')
  if (!target) return
  target.classList.add('is-initial-focus')
  target.addEventListener('blur', () => target.classList.remove('is-initial-focus'), { once: true })
  target.focus()
}

function hideLeavingStep(element) {
  element.inert = true
  element.setAttribute('aria-hidden', 'true')
}

watch(() => state.value.phase, async (phase, previous) => {
  if (phase === 'wifi' && previous !== 'wifi') {
    wifiReady.value = false
    // Keep the actual Wi-Fi rejection visible when returning from a test.
    // The previous network choices are still available for another attempt.
    if (!state.value.error) await scanNetworks()
    if (state.value.phase !== 'wifi') return
    wifiReady.value = true
  }
  await focusStep()
})


async function connect(transport = activeTransport.value) {
  if (!support.supported && !isDemo) return
  activeTransport.value = transport
  chooserCancelled.value = false
  const result = await session.connect(transport)
  chooserCancelled.value = result?.phase === 'ready'
  if (chooserCancelled.value) await focusStep()
}

async function scanNetworks() {
  if (scanPromise) return scanPromise
  scanBusy.value = true
  scanPromise = session.scanNetworks()
    .then((results) => { networks.value = results })
    .catch(() => { networks.value = [] })
    .finally(() => { scanBusy.value = false; scanPromise = null })
  return scanPromise
}

async function submitWifi(credentials) {
  wifiBusy.value = true
  await session.submitWifi(credentials)
  wifiBusy.value = false
}

async function close() {
  if (critical.value) return
  await session.cancel()
  emit('close')
}

function handleKeydown(event) {
  root.value?.querySelector('.is-initial-focus')?.classList.remove('is-initial-focus')
  if (event.key === 'Escape' &&
      (reTerminalHelpOpen.value || event.target?.closest?.('[role="dialog"]'))) return
  if (event.key === 'Escape' && !critical.value) void close()
}

onMounted(() => { document.addEventListener('keydown', handleKeydown); void focusStep() })
onBeforeUnmount(() => { unsubscribe(); document.removeEventListener('keydown', handleKeydown); void session.cancel() })
</script>

<template>
  <section
    id="installer-flow"
    ref="root"
    class="installer-layer"
    :data-phase="displayPhase"
    :data-has-error="Boolean(state.error)"
    aria-labelledby="installer-title"
  >
    <button class="installer-back" type="button" aria-label="Back to configurator" :disabled="critical" @click="close">
      <svg aria-hidden="true" focusable="false" viewBox="0 0 20 20"><path d="m12.5 5-5 5 5 5" /></svg>
    </button>
    <div class="installer-live-region" role="status" aria-live="polite">{{ progressCopy[0] }}</div>

    <InstallerStateIcon :phase="displayPhase === 'verification-issue' ? 'error' : displayPhase" />

    <div class="installer-stage">
      <Transition name="installer-step-slide" @before-leave="hideLeavingStep">
        <div :key="displayPhase" ref="activeView" class="installer-stage__view">
          <InstallerConnect
            v-if="state.phase === 'ready'"
            :device-label="deviceLabel"
            :unsupported-reason="unsupportedReason"
            :active-transport="activeTransport"
            :alternate-transport="fallbackTransport"
            :chooser-cancelled="chooserCancelled"
            @buy="reTerminalHelpOpen = true"
            @connect="connect"
          />

          <div v-else-if="state.phase === 'choosing-device'" class="installer-step installer-step--choose-device" aria-busy="true">
            <div class="installer-step__copy">
              <h2 id="installer-title">Select your reTerminal</h2>
              <p>In the browser window, select the connected device. It may appear as USB Serial or a similar USB name.</p>
            </div>
          </div>

          <div v-else-if="state.phase === 'checking-device'" class="installer-step" aria-busy="true">
            <div class="installer-step__copy">
              <h2 id="installer-title">{{ progressCopy[0] }}</h2>
              <p>{{ progressCopy[1] }}</p>
            </div>
          </div>

          <div v-else-if="state.phase === 'confirm-device'" class="installer-step">
            <div class="installer-step__copy">
              <h2 id="installer-title">Confirm your reTerminal</h2>
              <p>Make sure this is a {{ deviceLabel }}. Installing will replace its software and saved setup.</p>
            </div>
            <div class="installer-actions">
              <button data-autofocus class="installer-primary" type="button" @click="session.confirmDevice()">Install Windpeek</button>
            </div>
          </div>

          <div v-else-if="state.phase === 'reconnect'" class="installer-step">
            <div class="installer-step__copy">
              <h2 id="installer-title">Reconnect your device</h2>
              <p><InstallerDiagnosticStatus :status="state.diagnosticStatus" :reference="state.diagnosticReference" :report="state.diagnosticReport" :message="state.error?.message || 'Keep USB connected and select your device.'" /></p>
            </div>
            <div class="installer-actions">
              <button data-autofocus class="installer-primary" type="button" @click="session.reconnect()">Choose USB device</button>
            </div>
          </div>

          <InstallerWifi v-else-if="state.phase === 'wifi'" :networks="networks" :error="state.error?.message" :busy="wifiBusy || scanBusy" :scanning="scanBusy" :diagnostic-status="state.diagnosticStatus" :diagnostic-reference="state.diagnosticReference" :diagnostic-report="state.diagnosticReport" @submit="submitWifi" @rescan="scanNetworks" />
          <InstallerComplete v-else-if="state.phase === 'complete'" @done="close" />

          <div v-else-if="state.phase === 'verification-issue'" class="installer-step installer-step--error">
            <div class="installer-step__copy">
              <h2 id="installer-title">Setup didn’t finish</h2>
              <p role="alert">Wi-Fi is connected. <InstallerDiagnosticStatus :status="state.diagnosticStatus" :reference="state.diagnosticReference" :report="state.diagnosticReport" :message="state.error?.message" /></p>
            </div>
            <div class="installer-actions">
              <button data-autofocus class="installer-primary" type="button" @click="state.canRetrySetup ? session.retrySetup() : session.reconnect()">{{ state.canRetrySetup ? 'Try again' : 'Reconnect device' }}</button>
            </div>
          </div>

          <div v-else-if="state.phase === 'error'" class="installer-step installer-step--error">
            <div class="installer-step__copy">
              <h2 id="installer-title">Setup interrupted</h2>
              <p role="alert"><InstallerDiagnosticStatus :status="state.diagnosticStatus" :reference="state.diagnosticReference" :report="state.diagnosticReport" :message="state.error?.message" /></p>
              <p v-if="!state.safeToDisconnect" class="installer-connection-state">Keep USB connected while writing stops.</p>
            </div>
            <div v-if="state.safeToDisconnect" class="installer-actions">
              <button v-if="state.error?.recoverable !== false" data-autofocus class="installer-primary" type="button" @click="connect(activeTransport)">Try again</button>
              <button v-if="state.error?.recoverable === false" class="installer-primary" type="button" @click="close">Close</button>
              <button v-if="fallbackTransport && state.error?.recoverable !== false" class="installer-secondary" type="button" @click="connect(fallbackTransport)">Try another connection</button>
            </div>
          </div>

          <InstallerProgress v-else :title="progressCopy[0]" :message="progressCopy[1]" :progress="state.progress" />
        </div>
      </Transition>
    </div>

    <ReTerminalHelpDialog v-model:open="reTerminalHelpOpen" />
  </section>
</template>
