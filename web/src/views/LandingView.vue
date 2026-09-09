<script setup>
import { siteVariant, configuratorLink } from '../marketing/siteVariant'
import LandingHero from '../components/LandingHero.vue'
import ReTerminalComparison from '../components/ReTerminalComparison.vue'
const variant = siteVariant()
const configureHref = configuratorLink()
</script>

<template>
  <div class="landing-page">
    <main id="main-content">
      <header>
        <h1>{{ variant.title }}</h1>
        <p class="intro">{{ variant.intro }}</p>
      </header>

      <LandingHero />

      <section class="story" aria-label="About Windpeek">
        <p>{{ variant.story }}</p>
      </section>

      <section class="landing-section personalize" id="build">
        <ul class="facts">
          <li><strong>The whole picture</strong> — {{ variant.id === 'swell' ? 'swell, wind, weather, temperature and tide' : 'wind, waves, weather, temperature and tide' }}</li>
          <li><strong>Your screen, your choice</strong> — choose what to show and how it looks</li>
          <li><strong>Always in sight</strong> — five days that update without opening an app</li>
          <li><strong>Global &amp; regional models</strong> — choose your wind and wave forecasts</li>
          <li><strong>Any spot, worldwide</strong> — search the catalog or add a location on the map</li>
          <li><strong>Months between charges</strong> — built around low-power e-ink hardware</li>
        </ul>
      </section>

      <section class="landing-section purchase" id="setup">
        <h2>Choose your reTerminal</h2>
        <p>Windpeek is free software for all three models. Choose the screen size and display type that suits you.</p>

        <ReTerminalComparison />

        <div class="configure-step">
          <h2>Configure &amp; install</h2>
          <p>Choose your spot, customize your forecast screen, connect your reTerminal with USB and press Install.</p>
          <a class="button configure-action configure-action--desktop" :href="configureHref">Configure &amp; install</a>
          <button class="button configure-action configure-action--mobile" type="button" disabled>Configure &amp; install on desktop</button>
        </div>
      </section>

      <section class="landing-section faq">
        <h2>Questions before you start</h2>
        <div class="faq-list">
          <details>
            <summary>Will my spot be available?</summary>
            <p>You can search the spot catalog or add any location on the map. Wind forecasts are available worldwide. Regional models, wave data and tide availability depend on your spot.</p>
          </details>
          <details>
            <summary>Which forecast models can I use?</summary>
            <p>Start with Best Match for automatic selection, or choose wind and wave models separately in Advanced. Wind options include ECMWF, ICON, GFS and regional models for your spot. Wave options include MFWAM, GFS Wave and EWAM. Forecast data comes from <a href="https://open-meteo.com/" target="_blank" rel="noopener noreferrer">Open-Meteo</a>.</p>
          </details>
          <details>
            <summary>What can I show on the screen?</summary>
            <p>Show wind and waves as graphs or compact numbers, or hide either one. Wind includes gusts and direction; waves include swell height, direction and period. You can also show or hide weather, temperature and tide, and add a minimum wind threshold to the wind graph.</p>
          </details>
          <details>
            <summary>What do I need to install Windpeek?</summary>
            <p>A reTerminal E1001, E1002 or E1003, a USB data cable, 2.4 GHz Wi-Fi and a current desktop version of Firefox, Chrome or Edge. Configure the screen, connect the device and press Install.</p>
          </details>
          <details>
            <summary>What does Windpeek need after installation?</summary>
            <p>Only Wi-Fi for forecast updates and occasional charging. It needs no Windpeek account or subscription. Firmware updates are installed again over USB.</p>
          </details>
          <details>
            <summary>How can I help Windpeek?</summary>
            <p>Share Windpeek with other wind and watersports communities, report what could be better, or <a href="https://donate.stripe.com/6oU14o3Hy1Xg5C02291wY00" target="_blank" rel="noopener noreferrer">make a donation</a> to support hosting and future development.</p>
          </details>
        </div>
      </section>

      <footer>Windpeek is an independent project by <a href="https://emieljanson.com/">Emiel Janson</a>.</footer>
    </main>

  </div>
</template>

<style src="../styles/landing.css"></style>
