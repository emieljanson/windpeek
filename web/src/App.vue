<script setup>
import { defineAsyncComponent } from 'vue'
import { siteVariant } from './marketing/siteVariant'
import { isConfiguratorLocation } from './routes'

const ConfiguratorApp = defineAsyncComponent(() => import('./ConfiguratorApp.vue'))
const LandingView = defineAsyncComponent(() => import('./views/LandingView.vue'))
const showConfigurator = isConfiguratorLocation()
const variant = siteVariant()
document.title = showConfigurator
  ? `Configure ${variant.name}`
  : `${variant.name} — Always-on ${variant.id === 'swell' ? 'swell' : 'wind'} forecast`
for (const [selector, content] of [
  ['meta[name="description"]', variant.description],
  ['meta[property="og:title"]', variant.title],
  ['meta[property="og:description"]', variant.description],
  ['meta[property="og:site_name"]', variant.name],
]) document.querySelector(selector)?.setAttribute('content', content)
</script>

<template>
  <ConfiguratorApp v-if="showConfigurator" />
  <LandingView v-else />
</template>
