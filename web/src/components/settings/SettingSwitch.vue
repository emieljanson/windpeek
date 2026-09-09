<script setup>
import { computed, inject, ref, useId } from 'vue'
import { SwitchRoot } from 'reka-ui'

const props = defineProps({
  modelValue: { type: Boolean, required: true },
  disabled: { type: Boolean, default: false },
  name: { type: String, default: undefined },
  ariaLabel: { type: String, default: undefined },
  ariaLabelledby: { type: String, default: undefined },
  ariaDescribedby: { type: String, default: undefined },
  disabledReason: { type: String, default: '' },
  offLabel: { type: String, default: 'Hide' },
  onLabel: { type: String, default: 'Show' },
})

const emit = defineEmits(['update:modelValue'])
const row = inject('windpeek-setting-row', null)
const pointerFocus = ref(false)
const hasInteracted = ref(false)
const isDisabled = computed(() => props.disabled || row?.disabled?.value || false)
const tooltipId = `setting-switch-tooltip-${useId()}`
const hasDisabledReason = computed(() => isDisabled.value && Boolean(props.disabledReason))
const nativeDisabled = computed(() => isDisabled.value && !hasDisabledReason.value)
const describedBy = computed(() => [
  props.ariaDescribedby || row?.describedBy?.value,
  hasDisabledReason.value ? tooltipId : '',
].filter(Boolean).join(' ') || undefined)

function selectValue(value) {
  if (isDisabled.value || props.modelValue === value) return
  hasInteracted.value = true
  emit('update:modelValue', value)
}

function updateValue(value) {
  if (isDisabled.value) return
  hasInteracted.value = true
  emit('update:modelValue', value)
}
</script>

<template>
  <div class="setting-switch-shell">
    <SwitchRoot
      :id="row?.controlId"
      class="setting-switch"
      :class="{ 'is-pointer-focus': pointerFocus, 'has-interacted': hasInteracted }"
      :model-value="props.modelValue"
      :disabled="nativeDisabled"
      :name="props.name"
      :aria-disabled="isDisabled ? 'true' : undefined"
      :aria-label="props.ariaLabel"
      :aria-labelledby="props.ariaLabelledby || row?.labelId"
      :aria-describedby="describedBy"
      @pointerdown="pointerFocus = true"
      @keydown="pointerFocus = false"
      @blur="pointerFocus = false"
      @update:model-value="updateValue"
    >
      <span class="setting-switch__segment setting-switch__segment--off" @click.stop="selectValue(false)">{{ props.offLabel }}</span>
      <span class="setting-switch__segment setting-switch__segment--on" @click.stop="selectValue(true)">{{ props.onLabel }}</span>
      <span class="setting-switch__thumb" aria-hidden="true" />
    </SwitchRoot>
    <span v-if="hasDisabledReason" :id="tooltipId" class="setting-switch-tooltip" role="tooltip">
      {{ props.disabledReason }}
    </span>
  </div>
</template>
