<script setup>
import { computed } from 'vue'

const DEFAULT_DONATION_URL = 'https://donate.stripe.com/6oU14o3Hy1Xg5C02291wY00'

const props = defineProps({
  donationUrl: {
    type: String,
    default: import.meta.env.VITE_DONATION_URL || DEFAULT_DONATION_URL,
  },
})
defineEmits(['done'])

const donationUrl = computed(() => {
  try {
    const url = new URL(String(props.donationUrl).trim())
    return url.protocol === 'https:' ? url.href : ''
  } catch {
    return ''
  }
})

function donate() {
  if (!donationUrl.value) return
  window.open(donationUrl.value, '_blank', 'noopener,noreferrer')
}
</script>

<template>
  <div class="installer-step installer-step--complete">
    <div class="installer-step__copy">
      <h2 id="installer-title">Ready for the wind</h2>
      <p v-if="donationUrl">Windpeek is free, so you can decide what it’s worth to you. If it’s useful, a donation can help make future features possible.</p>
      <p v-else>The selected spot and display options are live on your device.</p>
    </div>
    <div class="installer-actions">
      <button v-if="donationUrl" class="installer-primary" type="button" @click="donate">Donate</button>
      <button data-autofocus class="installer-primary" type="button" @click="$emit('done')">Done</button>
    </div>
  </div>
</template>
