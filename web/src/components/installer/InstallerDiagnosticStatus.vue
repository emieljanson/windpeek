<script setup>
import { computed } from 'vue'
import { isInstallerDiagnosticReference } from '../../installer/sentryReporter'

const props = defineProps({
  status: { type: String, default: 'idle' },
  reference: { type: String, default: '' },
  report: { type: String, default: '' },
  message: { type: String, default: '' },
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
    : props.report ? 'I downloaded the Windpeek diagnostic report and will attach it to this email.'
      : 'Please help me finish setting up my Windpeek.'
  return `mailto:emiel@emieljanson.com?subject=${encodeURIComponent(subject)}&body=${encodeURIComponent(body)}`
})
</script>

<template>
  <span v-if="message || report || status !== 'idle'" class="installer-diagnostics">
    <template v-if="message">{{ message.replace(/\s+/g, ' ').trim() }}{{ ' ' }}</template>
    <template v-if="report"><a :href="`data:application/json;charset=utf-8,${encodeURIComponent(report)}`" download="windpeek-diagnostic.json">Download the report</a> and <a :href="emailHref">email it to support</a>.</template>
    <a v-else :href="emailHref">Email support</a><template v-if="!report">.</template>
  </span>
</template>
