<script setup>
import { computed, watch } from 'vue'
import { Toaster, toast } from 'vue-sonner'
import '@fontsource-variable/jetbrains-mono'
import 'vue-sonner/style.css'
import './styles/settings-controls.css'
import './styles/reterminal-help-dialog.css'
import './styles/spot-dialog.css'
import './styles/installer.css'
import ConfiguratorView from './views/ConfiguratorView.vue'
import { useCompactViewport } from './composables/useCompactViewport'
import { useConfiguratorStore } from './stores/configurator'

const store = useConfiguratorStore()
const { isCompact } = useCompactViewport()
const toasterPosition = computed(() => isCompact.value ? 'top-center' : 'bottom-right')

watch(
  () => [store.forecastStatus, store.forecastMessage],
  ([status, message]) => {
    if (status === 'warning' && message) {
      toast.error(message, { id: 'forecast-error' })
      return
    }
    toast.dismiss('forecast-error')
  },
  { flush: 'post' },
)
</script>

<template>
  <ConfiguratorView />
  <Toaster
    :position="toasterPosition"
    :visible-toasts="3"
    :toast-options="{ duration: 5000 }"
  />
</template>
