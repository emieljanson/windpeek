<script setup>
import { publicAssetUrl } from '../assets/publicAssetUrl'

const hardwareModels = [
  {
    model: 'E1001',
    screen: '7.5″, 4 greys',
    resolution: '800 × 480',
    threshold: 'Black threshold',
    battery: '3 month battery',
    batteryCompact: '3 mo battery',
    price: '~$74',
    image: publicAssetUrl('devices/previews/e1001.png'),
    buyUrl: 'https://www.seeedstudio.com/reTerminal-E1001-p-6534.html?sensecap_affiliate=UF4PmgK&referring_service=link',
  },
  {
    model: 'E1002',
    screen: '7.3″, 6 colours',
    resolution: '800 × 480',
    threshold: 'Red threshold',
    battery: '3 month battery',
    batteryCompact: '3 mo battery',
    price: '~$107',
    image: publicAssetUrl('devices/previews/e1002.png'),
    buyUrl: 'https://www.seeedstudio.com/reTerminal-E1002-p-6533.html?sensecap_affiliate=UF4PmgK&referring_service=link',
  },
  {
    model: 'E1003',
    screen: '10.3″, 16 greys',
    resolution: '1872 × 1404',
    threshold: 'Black threshold',
    battery: '6 month battery',
    batteryCompact: '6 mo battery',
    price: '~$160',
    image: publicAssetUrl('devices/previews/e1003.png'),
    buyUrl: 'https://www.seeedstudio.com/reTerminal-E1003-p-6731.html?sensecap_affiliate=UF4PmgK&referring_service=link',
  },
]

const hardwareSpecs = [
  { id: 'screen', label: 'Screen', keys: ['screen', 'resolution'] },
  { id: 'threshold', label: 'Threshold line', keys: ['threshold'] },
  { id: 'battery', label: 'Battery', keys: ['battery'] },
]
</script>

<template>
  <div class="reterminal-comparison">
<ul class="hardware-models">
          <li v-for="device in hardwareModels" :key="device.model" class="hardware-model">
            <div class="hardware-model__visual">
              <img :src="device.image" alt="" loading="lazy" decoding="async">
            </div>
            <p class="hardware-model__name">{{ device.model }}</p>
          </li>
        </ul>

        <dl class="hardware-specs" aria-label="reTerminal comparison">
          <div v-for="spec in hardwareSpecs" :key="spec.id" :class="['hardware-spec', `hardware-spec--${spec.id}`]">
            <dt>{{ spec.label }}</dt>
            <dd v-for="device in hardwareModels" :key="device.model">
              <span
                v-for="key in spec.keys"
                :key="key"
                class="hardware-spec__line"
              >
                <span :class="{ 'hardware-spec__copy--desktop': key === 'battery' }">{{ device[key] }}</span>
                <span v-if="key === 'battery'" class="hardware-spec__copy--mobile">{{ device.batteryCompact }}</span>
              </span>
            </dd>
          </div>
        </dl>

        <ul class="hardware-buys" aria-label="Buy a reTerminal">
          <li v-for="device in hardwareModels" :key="device.model">
            <a
              class="button hardware-model__buy"
              :href="device.buyUrl"
              :aria-label="`Buy reTerminal ${device.model}, approximately ${device.price.replace('~', '')}`"
              target="_blank"
              rel="sponsored noopener noreferrer"
            >Buy for {{ device.price }}</a>
          </li>
        </ul>
  </div>
</template>

<style src="../styles/reterminal-comparison.css"></style>
