<script setup>
import LandingHero from '../components/LandingHero.vue'
import ReTerminalComparison from '../components/ReTerminalComparison.vue'
</script>

<template>
  <div class="landing-page">
    <main id="main-content">
      <header>
        <h1>The always-on wind forecast for your favorite spot</h1>
        <p class="intro">Windpeek turns a reTerminal into a quiet e-ink display that keeps the next five days visible at home.</p>
      </header>

      <LandingHero />

      <section class="story" aria-label="About Windpeek">
        <p>Wind forecasts change. You check once, it looks like nothing, and later discover it turned into a great session. Windpeek keeps the whole forecast in sight, so you catch the change instead of hearing about it afterwards.</p>
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
        <p>Windpeek is free software for all three models. Choose the screen size and display type that suits you.</p>

        <ReTerminalComparison />

        <div class="configure-step">
          <h2>Configure &amp; install</h2>
          <p>Installing Windpeek is simple: choose your spot, connect your reTerminal with USB and press Install.</p>
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
            <summary>Will Windpeek show swell?</summary>
            <p>Not yet. Swell support is planned, so Windpeek can eventually show more of the conditions that determine whether a session is worth it.</p>
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
