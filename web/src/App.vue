<script setup>
import { defineAsyncComponent } from 'vue'
import { siteVariant } from './marketing/siteVariant'
import { isConfiguratorLocation } from './routes'

const ConfiguratorApp = defineAsyncComponent(() => import('./ConfiguratorApp.vue'))
const LandingView = defineAsyncComponent(() => import('./views/LandingView.vue'))
const showConfigurator = isConfiguratorLocation()
const variant = siteVariant()
// Copying the swell landing address should share the crawler-readable page.
if (import.meta.env.PROD && !showConfigurator && variant.id === 'swell' &&
    !/\/swell\/?$/.test(location.pathname)) {
  const shared = new URL('swell/', location.href)
  shared.search = location.search
  shared.searchParams.delete('site')
  shared.hash = location.hash
  if (!document.querySelector('base')) {
    const base = document.createElement('base')
    base.href = new URL('.', location.href).href
    document.head.prepend(base)
  }
  history.replaceState(history.state, '', shared)
}
const skipLink = document.querySelector('.skip-link')
if (skipLink) skipLink.href = `${location.pathname}${location.search}#main-content`
document.title = showConfigurator
  ? `Configure ${variant.name}`
  : `${variant.name} — Always-on ${variant.id === 'swell' ? 'swell' : 'wind'} forecast`
for (const [selector, content] of [
  ['meta[name="description"]', variant.description],
  ['meta[property="og:title"]', variant.title],
  ['meta[property="og:description"]', variant.description],
  ['meta[property="og:site_name"]', variant.name],
  ['meta[property="og:url"]', variant.id === 'swell' ? 'https://windpeek.com/swell/' : 'https://windpeek.com/'],
  ['meta[property="og:image"]', `https://windpeek.com/marketing/${variant.id === 'swell' ? 'windpeek-social-swell-v1.jpg' : 'windpeek-social-v17.jpg'}`],
]) document.querySelector(selector)?.setAttribute('content', content)
document.querySelector('link[rel="canonical"]')?.setAttribute('href',
  variant.id === 'swell' ? 'https://windpeek.com/swell/' : 'https://windpeek.com/')
</script>

<template>
  <ConfiguratorApp v-if="showConfigurator" />
  <LandingView v-else />
</template>
