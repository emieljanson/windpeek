<script setup>
import { computed, inject, ref, useId, watch } from 'vue'

const props = defineProps({
  modelValue: { type: Number, required: true },
  min: { type: Number, default: 5 },
  max: { type: Number, default: 35 },
  step: { type: Number, default: 1 },
  unit: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
  name: { type: String, default: undefined },
  ariaLabel: { type: String, default: undefined },
  ariaLabelledby: { type: String, default: undefined },
  ariaDescribedby: { type: String, default: undefined },
})

const emit = defineEmits(['update:modelValue'])
const row = inject('windpeek-setting-row', null)
const isDisabled = computed(() => props.disabled || row?.disabled?.value || false)
const draft = ref(String(props.modelValue))
const invalid = ref(false)
const focused = ref(false)
const pointerFocus = ref(false)
const errorId = `setting-number-error-${useId()}`
const unitId = `setting-number-unit-${useId()}`

const rangeError = computed(() => {
  const suffix = props.unit ? ` ${props.unit}` : ''
  return `Enter a value from ${props.min} to ${props.max}${suffix}`
})

const describedBy = computed(() => [
  props.ariaDescribedby || row?.describedBy?.value,
  props.unit ? unitId : '',
  invalid.value ? errorId : '',
].filter(Boolean).join(' ') || undefined)

watch(() => props.modelValue, (value) => {
  if (!focused.value) draft.value = String(value)
})

function parseValidValue(value) {
  if (value.trim() === '') return null
  const number = Number(value)
  if (!Number.isFinite(number) || number < props.min || number > props.max) return null
  const offset = (number - props.min) / props.step
  if (!Number.isInteger(Math.round(offset * 1e9) / 1e9)) return null
  return number
}

function updateDraft(event) {
  draft.value = event.target.value
  const value = parseValidValue(draft.value)
  invalid.value = value == null
  if (value != null) emit('update:modelValue', value)
}

function rollback() {
  draft.value = String(props.modelValue)
  invalid.value = false
}

function handleBlur() {
  focused.value = false
  pointerFocus.value = false
  if (parseValidValue(draft.value) == null) rollback()
}

function handleEscape(event) {
  event.preventDefault()
  rollback()
}
</script>

<template>
  <div class="setting-number">
    <div class="setting-number__field">
      <input
        :id="row?.controlId"
        v-model="draft"
        class="setting-control setting-number__input"
        :class="{ 'is-pointer-focus': pointerFocus }"
        type="number"
        inputmode="numeric"
        :min="props.min"
        :max="props.max"
        :step="props.step"
        :name="props.name"
        :disabled="isDisabled"
        :aria-label="props.ariaLabel"
        :aria-labelledby="props.ariaLabelledby || row?.labelId"
        :aria-describedby="describedBy"
        :aria-invalid="invalid ? 'true' : undefined"
        @focus="focused = true"
        @pointerdown="pointerFocus = true"
        @keydown="pointerFocus = false"
        @input="updateDraft"
        @blur="handleBlur"
        @keydown.esc="handleEscape"
      >
      <span v-if="props.unit" :id="unitId" class="setting-number__unit">{{ props.unit }}</span>
    </div>
    <p v-if="invalid && focused" :id="errorId" class="setting-number__error" role="alert">
      {{ rangeError }}
    </p>
  </div>
</template>
