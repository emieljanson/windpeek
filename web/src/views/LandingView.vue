<script setup>
import LandingHero from '../components/LandingHero.vue'
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
  <div class="landing-page">
    <main id="main-content">
      <header>
        <h1>The always-on wind forecast for your favorite spot</h1>
        <p class="intro">Windscout turns a reTerminal into a quiet e-ink display that keeps the next five days visible at home.</p>
      </header>

      <LandingHero />

      <section class="story" aria-label="About Windscout">
        <p>Wind forecasts change. You check once, it looks like nothing, and later discover it turned into a great session. Windscout keeps the whole forecast in sight, so you catch the change instead of hearing about it afterwards.</p>
      </section>

      <section class="landing-section personalize" id="build">
        <ul class="facts">
          <li><strong>The whole picture</strong> — wind, gusts, direction, weather, temperature and tide</li>
          <li><strong>Always in sight</strong> — five days that update without opening an app</li>
          <li><strong>16 forecast models</strong> — worldwide ECMWF, ICON and GFS + 13 local models</li>
          <li><strong>Any spot, worldwide</strong> — search the catalog or add a location on the map</li>
          <li><strong>Months between charges</strong> — built around low-power e-ink hardware</li>
        </ul>
      </section>

      <section class="landing-section purchase" id="setup">
        <h2>Choose your reTerminal</h2>
        <p>Windscout is free software for all three models. Choose the screen size and display type that suits you.</p>

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
              target="_blank"
              rel="sponsored noopener noreferrer"
            >Buy for {{ device.price }}</a>
          </li>
        </ul>

        <div class="configure-step">
          <h2>Configure &amp; install</h2>
          <p>Installing Windscout is simple: choose your spot, connect your reTerminal with USB and press Install.</p>
          <a class="button configure-action configure-action--desktop" href="?configure">Configure &amp; install</a>
          <button class="button configure-action configure-action--mobile" type="button" disabled>Configure &amp; install on desktop</button>
        </div>
      </section>

      <section class="landing-section faq">
        <h2>Questions before you start</h2>
        <div class="faq-list">
          <details>
            <summary>Will my spot be available?</summary>
            <p>You can search the spot catalog or add any location on the map. Global forecast models cover spots worldwide; regional models and tide depend on location.</p>
          </details>
          <details>
            <summary>Which forecast models can I use?</summary>
            <p>Forecasts come from <a href="https://open-meteo.com/" target="_blank" rel="noopener noreferrer">Open-Meteo</a>. Choose worldwide models ECMWF, DWD ICON and NOAA GFS, or one of 13 high-resolution local models, including KNMI HARMONIE, DMI HARMONIE, AROME, UKV and HRRR, depending on your spot.</p>
          </details>
          <details>
            <summary>What can I show on the screen?</summary>
            <p>Wind, gusts and direction are always included. You can also show weather, temperature, tide and a wind threshold, and choose your units.</p>
          </details>
          <details>
            <summary>Will Windscout show swell?</summary>
            <p>Not yet. Swell support is planned, so Windscout can eventually show more of the conditions that determine whether a session is worth it.</p>
          </details>
          <details>
            <summary>What do I need to install Windscout?</summary>
            <p>A reTerminal E1001, E1002 or E1003, a USB data cable, 2.4 GHz Wi-Fi and a current desktop version of Firefox, Chrome or Edge. Configure the screen, connect the device and press Install.</p>
          </details>
          <details>
            <summary>What does Windscout need after installation?</summary>
            <p>Only Wi-Fi for forecast updates and occasional charging. It needs no Windscout account or subscription. Firmware updates are installed again over USB.</p>
          </details>
          <details>
            <summary>How can I help Windscout?</summary>
            <p>Share Windscout with other wind and watersports communities, report what could be better, or <a href="https://donate.stripe.com/6oU14o3Hy1Xg5C02291wY00" target="_blank" rel="noopener noreferrer">make a donation</a> to support hosting and future development.</p>
          </details>
        </div>
      </section>

      <footer>Windscout is an independent project by <a href="https://emieljanson.com/">Emiel Janson</a>.</footer>
    </main>

  </div>
</template>

<style src="../styles/landing.css"></style>
