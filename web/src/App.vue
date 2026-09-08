<script setup>
import { defineAsyncComponent } from 'vue'
import { isConfiguratorLocation } from './routes'

const ConfiguratorApp = defineAsyncComponent(() => import('./ConfiguratorApp.vue'))
const LandingView = defineAsyncComponent(() => import('./views/LandingView.vue'))
const showConfigurator = isConfiguratorLocation()
document.title = showConfigurator
  ? 'Configure Windpeek'
  : 'Windpeek — Always-on e-ink wind forecast'
</script>

<template>
  <ConfiguratorApp v-if="showConfigurator" />
  <LandingView v-else />
</template>
