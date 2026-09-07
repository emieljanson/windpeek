<script setup>
import { defineAsyncComponent } from 'vue'
import { isConfiguratorLocation, pageBackgroundForLocation } from './routes'

const ConfiguratorApp = defineAsyncComponent(() => import('./ConfiguratorApp.vue'))
const LandingView = defineAsyncComponent(() => import('./views/LandingView.vue'))
const showConfigurator = isConfiguratorLocation()
const pageBackground = pageBackgroundForLocation()
document.documentElement.style.setProperty('--page-background', pageBackground)
document.querySelector('meta[name="theme-color"]')?.setAttribute('content', pageBackground)
document.title = showConfigurator
  ? 'Configure Windscout'
  : 'Windscout — Always-on e-ink wind forecast'
</script>

<template>
  <ConfiguratorApp v-if="showConfigurator" />
  <LandingView v-else />
</template>
