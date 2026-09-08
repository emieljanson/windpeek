<script setup>
import { computed, provide, useId } from 'vue'

const props = defineProps({
  label: { type: String, required: true },
  description: { type: String, default: '' },
  error: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
})

const rowId = `setting-row-${useId()}`
const controlId = `${rowId}-control`
const labelId = `${rowId}-label`
const descriptionId = `${rowId}-description`
const errorId = `${rowId}-error`
const describedBy = computed(() => [
  props.description ? descriptionId : '',
  props.error ? errorId : '',
].filter(Boolean).join(' ') || undefined)

provide('windpeek-setting-row', {
  controlId,
  labelId,
  describedBy,
  disabled: computed(() => props.disabled),
})
</script>

<template>
  <div class="setting-row" :class="{ 'setting-row--disabled': props.disabled }">
    <div class="setting-row__copy">
      <div class="setting-row__label-line">
        <label :id="labelId" class="setting-row__label" :for="controlId">
          {{ props.label }}
        </label>
        <slot name="label-action" />
      </div>
      <p v-if="props.description" :id="descriptionId" class="setting-row__description">
        {{ props.description }}
      </p>
      <p v-if="props.error" :id="errorId" class="setting-row__error" role="alert">
        {{ props.error }}
      </p>
    </div>
    <div class="setting-row__control">
      <slot />
    </div>
  </div>
</template>
