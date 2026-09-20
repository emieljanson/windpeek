<script setup>
import { computed } from 'vue'
import { isInstallerDiagnosticReference } from '../../installer/sentryReporter'

const props = defineProps({
  status: { type: String, default: 'idle' },
  reference: { type: String, default: '' },
  report: { type: String, default: '' },
})

const confirmedReference = computed(() => (
  props.status === 'sent' && isInstallerDiagnosticReference(props.reference)
    ? props.reference
    : ''
))
</script>

<template>
  <p v-if="confirmedReference" class="installer-diagnostic-status">
    Diagnostic reference: <code>{{ confirmedReference }}</code>
  </p>
  <p v-if="status === 'failed' && report" class="installer-diagnostic-download" role="status">
    Technical details could not be sent.
    <a :href="`data:application/json;charset=utf-8,${encodeURIComponent(report)}`" download="windpeek-diagnostic.json">Download report</a>
    and include it in your support message.
  </p>
</template>
