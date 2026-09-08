<script setup>
import { computed, inject, ref } from 'vue'
import {
  SelectContent,
  SelectIcon,
  SelectItem,
  SelectItemIndicator,
  SelectItemText,
  SelectPortal,
  SelectRoot,
  SelectSeparator,
  SelectTrigger,
  SelectValue,
  SelectViewport,
} from 'reka-ui'

const props = defineProps({
  modelValue: { type: [String, Number], default: undefined },
  options: { type: Array, required: true },
  placeholder: { type: String, default: 'Select an option' },
  disabled: { type: Boolean, default: false },
  muted: { type: Boolean, default: false },
  name: { type: String, default: undefined },
  ariaLabel: { type: String, default: undefined },
  ariaLabelledby: { type: String, default: undefined },
  ariaDescribedby: { type: String, default: undefined },
  native: { type: Boolean, default: false },
  autofocus: { type: Boolean, default: false },
})

const emit = defineEmits(['update:modelValue'])
const row = inject('windpeek-setting-row', null)
const isDisabled = computed(() => props.disabled || row?.disabled?.value || false)
const pointerFocus = ref(false)

function updateNativeValue(event) {
  const selectedValue = event.target.value
  const option = props.options.find((candidate) => String(candidate.value) === selectedValue)
  emit('update:modelValue', option?.value ?? selectedValue)
}
</script>

<template>
  <div v-if="props.native" class="setting-select__native-shell">
    <select
      :id="row?.controlId"
      class="setting-control setting-select__native"
      :class="{ 'is-muted': props.muted, 'is-pointer-focus': pointerFocus }"
      :value="props.modelValue == null ? '' : String(props.modelValue)"
      :disabled="isDisabled"
      :name="props.name"
      :aria-label="props.ariaLabel"
      :aria-labelledby="props.ariaLabelledby || row?.labelId"
      :aria-describedby="props.ariaDescribedby || row?.describedBy?.value"
      :data-autofocus="props.autofocus ? '' : undefined"
      @pointerdown="pointerFocus = true"
      @keydown="pointerFocus = false"
      @blur="pointerFocus = false"
      @change="updateNativeValue"
    >
      <option v-if="props.modelValue == null" value="" disabled>{{ props.placeholder }}</option>
      <option
        v-for="option in props.options"
        :key="String(option.value)"
        :value="String(option.value)"
        :disabled="option.disabled"
      >
        {{ option.label }}
      </option>
    </select>
    <span class="setting-select__chevron setting-select__native-chevron" aria-hidden="true">
      <svg viewBox="0 0 16 16" fill="none" focusable="false">
        <path fill="currentColor" d="M4.53033 5.46967C4.23744 5.17678 3.76256 5.17678 3.46967 5.46967C3.17678 5.76256 3.17678 6.23744 3.46967 6.53033L7.46967 10.5303C7.76001 10.8207 8.22986 10.8236 8.52376 10.5368L12.5238 6.63419C12.8202 6.34493 12.8261 5.87009 12.5368 5.57361C12.2476 5.27713 11.7727 5.27128 11.4762 5.56054L8.00649 8.94583L4.53033 5.46967Z" />
      </svg>
    </span>
  </div>
  <SelectRoot
    v-else
    :model-value="props.modelValue"
    :disabled="isDisabled"
    :name="props.name"
    @update:model-value="emit('update:modelValue', $event)"
  >
    <SelectTrigger
      :id="row?.controlId"
      class="setting-control setting-select__trigger"
      :class="{ 'is-muted': props.muted, 'is-pointer-focus': pointerFocus }"
      :aria-label="props.ariaLabel"
      :aria-labelledby="props.ariaLabelledby || row?.labelId"
      :aria-describedby="props.ariaDescribedby || row?.describedBy?.value"
      :data-autofocus="props.autofocus ? '' : undefined"
      @pointerdown="pointerFocus = true"
      @keydown="pointerFocus = false"
      @blur="pointerFocus = false"
    >
      <SelectValue class="setting-select__value" :placeholder="props.placeholder" />
      <SelectIcon class="setting-select__chevron" aria-hidden="true">
        <svg viewBox="0 0 16 16" fill="none" focusable="false">
          <path fill="currentColor" d="M4.53033 5.46967C4.23744 5.17678 3.76256 5.17678 3.46967 5.46967C3.17678 5.76256 3.17678 6.23744 3.46967 6.53033L7.46967 10.5303C7.76001 10.8207 8.22986 10.8236 8.52376 10.5368L12.5238 6.63419C12.8202 6.34493 12.8261 5.87009 12.5368 5.57361C12.2476 5.27713 11.7727 5.27128 11.4762 5.56054L8.00649 8.94583L4.53033 5.46967Z" />
        </svg>
      </SelectIcon>
    </SelectTrigger>
    <SelectPortal>
      <SelectContent
        class="setting-popup setting-select__content"
        position="item-aligned"
        :body-lock="false"
        :collision-padding="2"
        @keydown="pointerFocus = false"
      >
        <SelectViewport class="setting-popup__viewport">
          <template v-for="option in props.options" :key="String(option.value)">
            <SelectSeparator
              v-if="option.separatorBefore"
              class="setting-select__separator"
            />
            <SelectItem
              class="setting-option"
              :value="option.value"
              :disabled="option.disabled"
            >
              <SelectItemText class="setting-option__text">{{ option.label }}</SelectItemText>
              <SelectItemIndicator class="setting-option__indicator" aria-hidden="true">
                <svg viewBox="0 0 16 16" fill="none" focusable="false">
                  <path fill="currentColor" d="M4.2996 7.23968C4.01775 6.93614 3.5432 6.91857 3.23966 7.20042C2.93613 7.48227 2.91856 7.95682 3.20041 8.26035L6.45041 11.7603C6.7612 12.095 7.29647 12.0766 7.58346 11.7212L12.8335 5.22127C13.0937 4.89904 13.0435 4.42683 12.7213 4.16657C12.399 3.9063 11.9268 3.95654 11.6665 4.27877L6.96051 10.1053L4.2996 7.23968Z" />
                </svg>
              </SelectItemIndicator>
            </SelectItem>
          </template>
        </SelectViewport>
      </SelectContent>
    </SelectPortal>
  </SelectRoot>
</template>
