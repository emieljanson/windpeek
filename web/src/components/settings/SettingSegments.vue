<script setup>
import { inject } from 'vue'
import { RadioGroupRoot, RadioGroupItem } from 'reka-ui'

defineProps({ modelValue: String, options: Array, name: String })
const emit = defineEmits(['update:modelValue'])
const row = inject('windpeek-setting-row', null)
</script>

<template>
  <RadioGroupRoot :id="row?.controlId" class="setting-segments" :name="name"
    :model-value="modelValue" :disabled="row?.disabled?.value" :aria-labelledby="row?.labelId"
    :aria-describedby="row?.describedBy?.value" orientation="horizontal"
    @update:model-value="emit('update:modelValue', $event)">
    <RadioGroupItem v-for="option in options" :key="option.value" :value="option.value"
      class="setting-segments__item">{{ option.label }}</RadioGroupItem>
  </RadioGroupRoot>
</template>

<style scoped>
.setting-segments {
  display: flex;
  padding: 2px;
  min-height: var(--settings-control-height);
  background: var(--settings-control-surface);
  border-radius: var(--settings-control-radius);
}
.setting-segments__item {
  flex: 1; min-width: 0; padding: 0; border: 0; background: transparent;
  color: var(--settings-control-muted); font: inherit; font-size: 0.8125rem; font-weight: 500;
  border-radius: calc(var(--settings-control-radius) - 2px); cursor: pointer;
}
.setting-segments__item[data-state='checked'] { background: var(--settings-strong-surface); color: var(--settings-control-ink); box-shadow: 0 1px 3px #00000018; }
.setting-segments__item:focus-visible { outline: 2px solid currentColor; outline-offset: 2px; }
</style>
