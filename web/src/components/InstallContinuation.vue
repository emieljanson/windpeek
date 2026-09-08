<script setup>
import { onMounted, ref } from 'vue'
import { createInstallerDemoSession } from '../installer/createInstallerDemoSession'
import InstallerPanel from './installer/InstallerPanel.vue'

const open = ref(false)
const trigger = ref(null)
const props = defineProps({
  configuration: { type: Object, required: true },
})
const emit = defineEmits(['open', 'close', 'installer-phase-change', 'usb-step-change'])
const testSessionFactory = import.meta.env.DEV
  ? globalThis.__WINDPEEK_INSTALLER_SESSION_FACTORY__
  : undefined
const demoEnabled = import.meta.env.DEV && new URLSearchParams(window.location.search).get('installerDemo') === '1'
const sessionFactory = demoEnabled ? createInstallerDemoSession : testSessionFactory
function start() {
  open.value = true
  emit('open')
}

function close() {
  open.value = false
  emit('usb-step-change', false)
  emit('installer-phase-change', 'ready')
  emit('close')
}

function restoreFocus() {
  trigger.value?.focus()
}

onMounted(() => {
  if (demoEnabled) start()
})
</script>

<template>
  <section class="install-continuation" aria-labelledby="install-entry-title">
    <button
      ref="trigger"
      data-testid="install-continuation"
      class="install-button"
      type="button"
      :aria-expanded="open"
      aria-controls="installer-flow"
      @click="start"
    >
      <span id="install-entry-title">Install</span>
    </button>
    <Transition name="installer-layer-transition" @after-leave="restoreFocus">
      <InstallerPanel
        v-if="open"
        :configuration="props.configuration"
        :session-factory="sessionFactory"
        @close="close"
        @installer-phase-change="emit('installer-phase-change', $event)"
        @usb-step-change="emit('usb-step-change', $event)"
      />
    </Transition>
  </section>
</template>
