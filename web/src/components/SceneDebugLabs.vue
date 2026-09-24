<script setup>
import { BOARD_IDS } from '../config/configuration'

defineProps({
  boardId: { type: String, required: true },
  cableLabEnabled: { type: Boolean, required: true },
  markingsLabEnabled: { type: Boolean, required: true },
  markingsLabOpen: { type: Boolean, required: true },
  cableLab: { type: Object, required: true },
  cableLabDuration: { type: Number, required: true },
  markingGroupLabels: { type: Object, required: true },
  markingOffsets: { type: Object, required: true },
  markingCopyStatus: { type: String, required: true },
  markingValuesJson: { type: String, required: true },
})
const emit = defineEmits(['runCable', 'camera', 'setMarkingsOpen', 'rearView', 'resetMarkings', 'copyMarkings'])
</script>

<template>
  <template v-if="cableLabEnabled || markingsLabEnabled">
    <aside v-if="cableLabEnabled" class="cable-lab" aria-label="Cable motion lab">
      <header>
        <strong>Cable motion lab</strong>
        <span>Temporary prototype</span>
      </header>

      <label>
        <span>Route distance <output>{{ cableLab.distance.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.distance" type="range" min="0.35" max="1.8" step="0.05">
      </label>
      <label>
        <span>3D speed <output>{{ cableLab.speed.toFixed(2) }}/s</output></span>
        <input v-model.number="cableLab.speed" type="range" min="0.15" max="0.9" step="0.05">
      </label>
      <p class="cable-lab__duration">Duration <strong>{{ cableLabDuration.toFixed(2) }}s</strong></p>

      <label>
        <span>Shared haze start <output>{{ cableLab.hazeStart.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.hazeStart" type="range" min="0.15" max="1.4" step="0.05">
      </label>
      <label>
        <span>Shared haze end <output>{{ cableLab.hazeEnd.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.hazeEnd" type="range" min="0.25" max="2.5" step="0.05">
      </label>
      <label>
        <span>Grid horizon start <output>{{ cableLab.gridHorizonStart.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.gridHorizonStart" type="range" min="0.4" max="2.5" step="0.1">
      </label>
      <label>
        <span>Grid horizon end <output>{{ cableLab.gridHorizonEnd.toFixed(2) }}</output></span>
        <input v-model.number="cableLab.gridHorizonEnd" type="range" min="0.8" max="5" step="0.1">
      </label>

      <div class="cable-lab__actions">
        <button type="button" @click="emit('runCable', true)">Insert</button>
        <button type="button" @click="emit('runCable', false)">Retract</button>
        <button type="button" @click="emit('camera', 'usb')">USB view</button>
        <button type="button" @click="emit('camera', 'cable')">Cable detail</button>
        <button type="button" @click="emit('camera', 'hero')">Hero view</button>
      </div>
    </aside>
    <button
      v-if="markingsLabEnabled && boardId === BOARD_IDS.E1002 && !markingsLabOpen"
      type="button"
      class="markings-lab-launch"
      @pointerdown.stop
      @click="emit('setMarkingsOpen', true)"
    >
      Markings
    </button>
    <aside
      v-if="markingsLabEnabled && boardId === BOARD_IDS.E1002 && markingsLabOpen"
      class="markings-lab"
      aria-label="E1002 marking position controls"
      @pointerdown.stop
    >
      <header>
        <strong>E1002 markings</strong>
        <button type="button" @click="emit('setMarkingsOpen', false)">Hide</button>
      </header>

      <section v-for="(label, group) in markingGroupLabels" :key="group">
        <strong>{{ label }}</strong>
        <label>
          <span>X</span>
          <input v-model.number="markingOffsets[group].x" type="range" min="-60" max="60" step="0.1">
          <input v-model.number="markingOffsets[group].x" type="number" min="-60" max="60" step="0.1">
        </label>
        <label>
          <span>Y</span>
          <input v-model.number="markingOffsets[group].y" type="range" min="-60" max="60" step="0.1">
          <input v-model.number="markingOffsets[group].y" type="number" min="-60" max="60" step="0.1">
        </label>
      </section>

      <div class="markings-lab__actions">
        <button type="button" @click="emit('rearView')">Rear view</button>
        <button type="button" @click="emit('resetMarkings')">Reset</button>
        <button type="button" class="markings-lab__copy" @click="emit('copyMarkings')">
          {{ markingCopyStatus || 'Copy values' }}
        </button>
      </div>
      <textarea
        class="markings-lab__values"
        :value="markingValuesJson"
        readonly
        aria-label="Marking offsets as JSON"
        @focus="$event.target.select()"
      />
    </aside>
  </template>
</template>

<style scoped>
.cable-lab {
  position: absolute;
  z-index: 10;
  inset: auto auto 1rem 1rem;
  width: min(18rem, calc(100% - 2rem));
  padding: 0.9rem;
  border: 1px solid rgb(255 255 255 / 72%);
  border-radius: 0.9rem;
  background: rgb(245 247 249 / 88%);
  box-shadow: 0 1rem 3rem rgb(31 38 43 / 14%);
  color: #20262b;
  cursor: default;
  backdrop-filter: blur(20px);
  max-height: calc(100% - 2rem);
  overflow: auto;
  scrollbar-width: thin;
}
.cable-lab header {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  align-items: baseline;
  margin-block-end: 0.75rem;
}
.cable-lab header strong { font-size: 0.8rem; }
.cable-lab header span,
.cable-lab__duration { color: #687078; font-size: 0.65rem; }
.cable-lab label { display: grid; gap: 0.2rem; margin-block: 0.55rem; }
.cable-lab label > span {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  font-size: 0.68rem;
}
.cable-lab output { font-family: 'JetBrains Mono Variable', monospace; }
.cable-lab input { width: 100%; accent-color: #70ad32; }
.cable-lab__duration { margin: -0.15rem 0 0.75rem; }
.cable-lab__actions { display: grid; grid-template-columns: 1fr 1fr; gap: 0.4rem; }
.cable-lab button {
  min-height: 2rem;
  border: 1px solid rgb(32 38 43 / 12%);
  border-radius: 0.55rem;
  background: #fff;
  color: inherit;
  font: 600 0.68rem/1 Inter, sans-serif;
  cursor: pointer;
}
.cable-lab button:active { transform: translateY(1px); }
.markings-lab {
  position: absolute;
  z-index: 12;
  inset: 0.75rem auto auto 0.75rem;
  width: min(18rem, calc(100% - 1.5rem));
  max-height: calc(100% - 1.5rem);
  overflow: auto;
  padding: 0.8rem;
  border-radius: 0.9rem;
  background: rgb(247 248 247 / 92%);
  box-shadow: 0 1rem 3rem rgb(20 24 22 / 18%), inset 0 0 0 1px rgb(0 0 0 / 8%);
  color: #202420;
  cursor: default;
  backdrop-filter: blur(18px);
}
.markings-lab header {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 0.75rem;
  margin-block-end: 0.55rem;
}
.markings-lab header strong { font-size: 0.78rem; }
.markings-lab header button {
  min-height: 1.65rem;
  padding-inline: 0.65rem;
  color: #5d635d;
}
.markings-lab section {
  padding-block: 0.5rem;
  border-block-start: 1px solid rgb(0 0 0 / 8%);
}
.markings-lab section > strong { display: block; margin-block-end: 0.35rem; font-size: 0.68rem; }
.markings-lab label {
  display: grid;
  grid-template-columns: 0.8rem 1fr 3.6rem;
  align-items: center;
  gap: 0.4rem;
  min-height: 1.8rem;
  font: 600 0.64rem/1 'JetBrains Mono Variable', monospace;
}
.markings-lab input[type='range'] { width: 100%; accent-color: #70ad32; }
.markings-lab input[type='number'] {
  width: 100%;
  min-height: 1.65rem;
  padding-inline: 0.35rem;
  border: 1px solid rgb(0 0 0 / 12%);
  border-radius: 0.4rem;
  background: #fff;
  color: inherit;
  font: inherit;
  text-align: right;
}
.markings-lab__actions { display: grid; grid-template-columns: 1fr 1fr; gap: 0.4rem; margin-block-start: 0.45rem; }
.markings-lab button {
  min-height: 2rem;
  border: 0;
  border-radius: 0.55rem;
  background: #fff;
  box-shadow: inset 0 0 0 1px rgb(0 0 0 / 10%);
  color: inherit;
  font: 600 0.68rem/1 Inter, sans-serif;
  cursor: pointer;
}
.markings-lab button:active { scale: 0.96; }
.markings-lab__copy { grid-column: 1 / -1; background: #171a17 !important; color: #fff !important; }
.markings-lab__values {
  width: 100%;
  height: 3.25rem;
  margin-block-start: 0.45rem;
  padding: 0.45rem;
  resize: vertical;
  border: 1px solid rgb(0 0 0 / 10%);
  border-radius: 0.5rem;
  background: rgb(255 255 255 / 75%);
  color: #505650;
  font: 500 0.58rem/1.35 'JetBrains Mono Variable', monospace;
}
.markings-lab-launch {
  position: absolute;
  z-index: 12;
  inset: 0.75rem auto auto 0.75rem;
  min-height: 2.25rem;
  padding-inline: 0.85rem;
  border: 0;
  border-radius: 0.65rem;
  background: #171a17;
  box-shadow: 0 0.75rem 2rem rgb(20 24 22 / 20%);
  color: #fff;
  font: 650 0.72rem/1 Inter, sans-serif;
  cursor: pointer;
}
.markings-lab-launch:active { scale: 0.96; }
@media (max-width: 40rem) {
  .markings-lab { max-height: 48%; }
}
</style>
