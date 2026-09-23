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
const emailHref = computed(() => {
  const subject = 'Windpeek installation help'
  const body = confirmedReference.value
    ? `My diagnostic reference is ${confirmedReference.value}. Please help me finish setup.`
    : 'I downloaded the Windpeek diagnostic report and will attach it to this email.'
  return `mailto:emiel@emieljanson.com?subject=${encodeURIComponent(subject)}&body=${encodeURIComponent(body)}`
})
</script>

<template>
  <p v-if="confirmedReference" class="installer-diagnostic-status">
    Diagnostic reference: <code>{{ confirmedReference }}</code>
  </p>
  <p v-if="status === 'failed' && report" class="installer-diagnostic-download" role="status">
    Technical details could not be sent.
    <a :href="`data:application/json;charset=utf-8,${encodeURIComponent(report)}`" download="windpeek-diagnostic.json">Download report</a>
    and <a :href="emailHref">email support</a>. Attach the downloaded file to the email.
  </p>
  <p v-else-if="confirmedReference" class="installer-diagnostic-download">
    <a :href="emailHref">Email support</a> about this installation.
  </p>
</template>
