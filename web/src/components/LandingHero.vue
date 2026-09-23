<script setup>
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import { siteVariant, configuratorLink } from '../marketing/siteVariant'
import { publicAssetUrl } from '../assets/publicAssetUrl'
import { LANDING_HERO_PRESENTATION } from '../marketing/landingHeroPresentation'

const {
  width: WIDTH,
  framing: HERO_FRAMING,
} = LANDING_HERO_PRESENTATION
const heroImage = publicAssetUrl(LANDING_HERO_PRESENTATION.imageFallback)
const heroImageWebp = publicAssetUrl(LANDING_HERO_PRESENTATION.imageWebp)
const heroImageSrcset = [
  ...LANDING_HERO_PRESENTATION.responsiveWebp.map(({ image, width }) => `${publicAssetUrl(image)} ${width}w`),
  `${heroImageWebp} ${WIDTH}w`,
].join(', ')
// Match the 642px scene / 24px viewport gutter, including the calibrated zoom.
const zoomedSize = size => Math.round(size * HERO_FRAMING.zoom * 100) / 100
const heroImageSizes = `(max-width: 666px) calc(${zoomedSize(100)}vw - ${zoomedSize(24)}px), ${zoomedSize(642)}px`
const heroMediaStyle = Object.freeze({
  '--hero-zoom': HERO_FRAMING.zoom,
  '--hero-focus-x': `${-HERO_FRAMING.focusX}%`,
  '--hero-focus-y': `${-HERO_FRAMING.focusY}%`,
})

const variant = siteVariant()
const configureHref = configuratorLink()
const photo = ref(null)
const LiveForecast = shallowRef(null)
let scheduled
let unmounted = false

async function loadLiveForecast() {
  try {
    const module = await import('./LandingForecast.vue')
    if (!unmounted) LiveForecast.value = module.default
  } catch { /* Keep the product photo usable if the enhancement cannot load. */ }
}

// Let the high-priority photo finish before competing for bandwidth and CPU.
function scheduleLiveForecast() {
  if (scheduled !== undefined || unmounted) return
  scheduled = window.requestIdleCallback
    ? window.requestIdleCallback(loadLiveForecast, { timeout: 1500 })
    : window.setTimeout(loadLiveForecast, 0)
}

onMounted(() => {
  if (photo.value?.complete) scheduleLiveForecast()
})

onBeforeUnmount(() => {
  unmounted = true
  if (window.requestIdleCallback) window.cancelIdleCallback(scheduled)
  else window.clearTimeout(scheduled)
})
</script>

<template>
  <figure class="landing-hero">
    <a class="hero-link" :href="configureHref" aria-label="Open the forecast configurator">
      <div class="hero-scene">
        <div class="hero-media" :style="heroMediaStyle">
          <picture>
            <source :srcset="heroImageSrcset" :sizes="heroImageSizes" type="image/webp">
            <img
              ref="photo"
              :src="heroImage"
              width="1672"
              height="941"
              :alt="`${variant.name} on a yellow sideboard showing a five-day ${variant.id === 'swell' ? 'swell' : 'wind'} forecast`"
              fetchpriority="high"
              @load="scheduleLiveForecast"
              @error="scheduleLiveForecast"
            >
          </picture>
          <component :is="LiveForecast" v-if="LiveForecast" />
        </div>
      </div>
    </a>
  </figure>
</template>
