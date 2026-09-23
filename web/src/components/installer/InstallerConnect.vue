<script setup>
defineProps({
  deviceLabel: { type: String, default: 'reTerminal E1001 or E1002' },
  unsupportedReason: { type: String, default: '' },
  preferredTransport: { type: String, default: 'serial' },
  alternateTransport: { type: String, default: '' },
  chooserCancelled: { type: Boolean, default: false },
})
defineEmits(['buy', 'connect'])
</script>

<template>
  <div class="installer-step installer-step--connect">
    <div class="installer-step__copy">
      <template v-if="unsupportedReason">
        <h2 id="installer-title">Use Firefox, Chrome, or Edge</h2>
        <p role="status">{{ unsupportedReason }}</p>
      </template>
      <template v-else-if="chooserCancelled && alternateTransport">
        <h2 id="installer-title">Device not listed?</h2>
        <p>Keep your {{ deviceLabel }} connected. Try another way to connect, without installing anything.</p>
      </template>
      <template v-else>
        <h2 id="installer-title">Connect your reTerminal</h2>
        <p>Connect your {{ deviceLabel }} with a USB data cable.</p>
      </template>
    </div>
    <div v-if="!unsupportedReason" class="installer-actions">
      <button
        v-if="!chooserCancelled || !alternateTransport"
        class="installer-secondary"
        type="button"
        aria-haspopup="dialog"
        @click="$emit('buy')"
      >
        Buy a reTerminal
      </button>
      <template v-if="chooserCancelled && alternateTransport">
        <button class="installer-secondary" type="button" @click="$emit('connect', preferredTransport)">Try again</button>
        <button data-autofocus class="installer-primary" type="button" @click="$emit('connect', alternateTransport)">Try another connection</button>
      </template>
      <button v-else data-autofocus class="installer-primary" type="button" @click="$emit('connect', preferredTransport)">
        Continue
      </button>
    </div>
  </div>
</template>
