<script setup>
import { computed, ref } from 'vue'
import SettingSelect from '../settings/SettingSelect.vue'
import InstallerDiagnosticStatus from './InstallerDiagnosticStatus.vue'

const props = defineProps({
  networks: { type: Array, default: () => [] },
  error: { type: String, default: '' },
  busy: Boolean,
  scanning: Boolean,
  diagnosticStatus: { type: String, default: 'idle' },
  diagnosticReference: { type: String, default: '' },
})
const emit = defineEmits(['submit', 'rescan'])
const ssid = ref('')
const password = ref('')
const describedBy = computed(() => props.error ? 'installer-wifi-help installer-wifi-error' : 'installer-wifi-help')
const networkOptions = computed(() => props.networks.map((network) => ({
  label: network.ssid,
  value: network.ssid,
})))
const selectedNetwork = computed(() => props.networks.find((network) => network.ssid === ssid.value))
const passwordRequired = computed(() => selectedNetwork.value?.secured !== false)
const canSubmit = computed(() => Boolean(
  ssid.value.trim() && (!passwordRequired.value || password.value),
))

function selectNetwork(value) {
  ssid.value = value
  if (props.networks.find((network) => network.ssid === value)?.secured === false) {
    password.value = ''
  }
}

function submit() {
  if (!canSubmit.value) return
  emit('submit', {
    ssid: ssid.value.trim(),
    password: passwordRequired.value ? password.value : '',
  })
  ssid.value = ''
  password.value = ''
}
</script>

<template>
  <form class="installer-step installer-wifi" @submit.prevent="submit">
    <div class="installer-step__copy">
      <h2 id="installer-title">Select a network for Windpeek</h2>
      <p id="installer-wifi-help">The device uses this connection to update its forecast.</p>
    </div>
    <div class="installer-fields">
      <div class="installer-field">
        <span>WiFi</span>
        <SettingSelect
          v-if="networks.length"
          :model-value="ssid || undefined"
          :options="networkOptions"
          aria-label="Wi-Fi network"
          :aria-describedby="describedBy"
          placeholder="Select"
          autofocus
          @update:model-value="selectNetwork"
        />
        <input v-else v-model="ssid" class="setting-control installer-field__control" name="ssid" type="text" autocomplete="off" spellcheck="false" :aria-describedby="describedBy" placeholder="Network name" required data-autofocus />
      </div>
      <label class="installer-field">
        <span>Password</span>
        <input
          v-model="password"
          class="setting-control installer-field__control"
          name="wifi-password"
          type="password"
          autocomplete="off"
          data-1p-ignore="true"
          data-lpignore="true"
          data-bwignore="true"
          data-form-type="other"
          :aria-invalid="Boolean(error)"
          :aria-describedby="describedBy"
          :required="passwordRequired"
        />
      </label>
      <p v-if="error" id="installer-wifi-error" class="installer-message is-error">{{ error }}</p>
      <InstallerDiagnosticStatus :status="diagnosticStatus" :reference="diagnosticReference" />
    </div>
    <div class="installer-actions">
      <button class="installer-secondary" type="button" :disabled="busy || scanning" @click="$emit('rescan')">{{ scanning ? 'Scanning…' : 'Scan again' }}</button>
      <button class="installer-primary" type="submit" :disabled="busy || !canSubmit">{{ busy ? 'Connecting…' : 'Continue' }}</button>
    </div>
  </form>
</template>
