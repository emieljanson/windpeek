<script setup>
import { computed, inject, nextTick, ref, watch, watchEffect } from 'vue'
import {
  ComboboxAnchor,
  ComboboxContent,
  ComboboxInput,
  ComboboxItem,
  ComboboxItemIndicator,
  ComboboxPortal,
  ComboboxRoot,
  ComboboxViewport,
} from 'reka-ui'

const CREATE_VALUE = '__windpeek-create-option__'

const props = defineProps({
  modelValue: { type: [String, Number, Object], default: undefined },
  searchTerm: { type: String, required: true },
  options: { type: Array, required: true },
  getOptionValue: { type: Function, default: (option) => option?.value ?? option },
  getOptionLabel: { type: Function, default: (option) => option?.label ?? String(option ?? '') },
  getOptionDescription: { type: Function, default: () => '' },
  displayValue: { type: Function, default: undefined },
  by: { type: [String, Function], default: undefined },
  placeholder: { type: String, default: 'Search' },
  emptyText: { type: String, default: 'No results found' },
  emptyRole: { type: String, default: 'status' },
  loadingText: { type: String, default: 'Searching…' },
  loading: { type: Boolean, default: false },
  createActionLabel: { type: String, default: '' },
  minSearchLength: { type: Number, default: 0 },
  keepSelectionLabel: { type: Boolean, default: false },
  restoreSearchOnClose: { type: Boolean, default: true },
  openOnFocus: { type: Boolean, default: true },
  suppressInitialFocusRing: { type: Boolean, default: false },
  showSearchIcon: { type: Boolean, default: false },
  showSelectionIndicator: { type: Boolean, default: true },
  selectAllOnFocus: { type: Boolean, default: false },
  inlineResults: { type: Boolean, default: false },
  blurAfterSelect: { type: Boolean, default: false },
  blurAfterDismiss: { type: Boolean, default: false },
  inputType: { type: String, default: 'text' },
  inputMode: { type: String, default: undefined },
  disabled: { type: Boolean, default: false },
  name: { type: String, default: undefined },
  ariaLabel: { type: String, default: undefined },
  ariaLabelledby: { type: String, default: undefined },
  ariaDescribedby: { type: String, default: undefined },
})

const emit = defineEmits([
  'update:modelValue',
  'update:searchTerm',
  'update:open',
  'search-intent',
  'create',
  'focus',
  'dismiss',
])
const row = inject('windpeek-setting-row', null)
const isDisabled = computed(() => props.disabled || row?.disabled?.value || false)
const canOpen = computed(() => props.searchTerm.trim().length >= props.minSearchLength)
const open = ref(false)
const input = ref(null)
const selectionPending = ref(false)
const cachedValue = ref(undefined)
const cachedLabel = ref('')
const initialFocusRingSuppressed = ref(props.suppressInitialFocusRing)
const pointerFocus = ref(false)

function valuesMatch(left, right) {
  if (Object.is(left, right)) return true
  if (left == null || right == null) return false
  if (typeof props.by === 'function') return props.by(left, right)
  if (typeof props.by === 'string') return Object.is(left?.[props.by], right?.[props.by])
  return String(left) === String(right)
}

function committedLabel(value = props.modelValue) {
  if (props.displayValue) return props.displayValue(value)
  return optionLabel(value)
}

function optionLabel(value) {
  const option = props.options.find((candidate) => valuesMatch(props.getOptionValue(candidate), value))
  if (option) return props.getOptionLabel(option)
  if (value && typeof value === 'object') return props.getOptionLabel(value)
  if (valuesMatch(value, cachedValue.value) && cachedLabel.value) return cachedLabel.value
  return value == null ? '' : String(value)
}

function optionKey(option) {
  const value = props.getOptionValue(option)
  return String(value && typeof value === 'object' ? value.id ?? props.getOptionLabel(option) : value)
}

watchEffect(() => {
  const value = props.modelValue
  if (!valuesMatch(value, cachedValue.value)) {
    cachedValue.value = value
    cachedLabel.value = ''
  }
  const option = props.options.find((candidate) => valuesMatch(props.getOptionValue(candidate), value))
  if (props.displayValue) cachedLabel.value = props.displayValue(value)
  else if (option) cachedLabel.value = props.getOptionLabel(option)
  else if (value && typeof value === 'object') cachedLabel.value = props.getOptionLabel(value)
})

watch(() => props.searchTerm, () => {
  if (!canOpen.value) {
    if (open.value) setOpen(false, { restore: false })
    return
  }
  if (!selectionPending.value && document.activeElement === input.value?.$el) setOpen(true)
}, { flush: 'post' })

function restoreCommittedLabel() {
  const restored = committedLabel()
  if (props.searchTerm !== restored) emit('update:searchTerm', restored)
}

function setOpen(value, { restore = true } = {}) {
  const blockedOpen = value && !canOpen.value
  const nextOpen = Boolean(value && canOpen.value)
  open.value = nextOpen
  emit('update:open', nextOpen)
  if (!nextOpen && !blockedOpen && restore && !selectionPending.value && props.restoreSearchOnClose) {
    restoreCommittedLabel()
  }
}

async function selectValue(value) {
  selectionPending.value = true
  if (value === CREATE_VALUE) {
    emit('create', props.searchTerm.trim())
    setOpen(false)
    await nextTick()
    selectionPending.value = false
    if (props.blurAfterSelect) input.value?.$el?.blur()
    else input.value?.$el?.focus()
    return
  }
  emit('update:modelValue', value)
  const label = props.keepSelectionLabel
    ? optionLabel(value)
    : props.restoreSearchOnClose ? committedLabel(value) : ''
  emit('update:searchTerm', label)
  setOpen(false)
  await nextTick()
  selectionPending.value = false
  if (props.blurAfterSelect) input.value?.$el?.blur()
  else input.value?.$el?.focus()
}

async function dismiss({ blur = props.blurAfterDismiss, notify = true } = {}) {
  setOpen(false)
  await nextTick()
  if (blur) input.value?.$el?.blur()
  if (notify) emit('dismiss')
}

function handleFocus(event) {
  emit('focus', event)
  if (!props.selectAllOnFocus) return
  const element = event.currentTarget
  nextTick(() => element?.select())
}

function handleBlur() {
  initialFocusRingSuppressed.value = false
  pointerFocus.value = false
}

defineExpose({
  focus: () => input.value?.$el?.focus(),
  dismiss,
})
</script>

<template>
  <div :class="{ 'setting-combobox--inline': props.inlineResults }">
    <ComboboxRoot
    :model-value="props.modelValue"
    :open="open"
    :disabled="isDisabled"
    :name="props.name"
    :by="props.by"
    :ignore-filter="true"
    :open-on-focus="props.openOnFocus"
    :open-on-click="canOpen"
    :reset-search-term-on-blur="false"
    :reset-search-term-on-select="false"
    @update:model-value="selectValue"
    @update:open="setOpen"
    >
    <ComboboxAnchor class="setting-combobox__anchor">
      <span v-if="props.showSearchIcon" class="setting-combobox__search-icon" aria-hidden="true">
        <svg viewBox="0 0 16 16" fill="none" focusable="false">
          <path fill="currentColor" fill-rule="evenodd" clip-rule="evenodd" d="M7 1.99805C9.76142 1.99805 12 4.23662 12 6.99805C12 8.10816 11.6375 9.13324 11.0254 9.96289L13.7803 12.7178L13.832 12.7744C14.0723 13.069 14.0549 13.5037 13.7803 13.7783C13.5057 14.0529 13.0709 14.0704 12.7764 13.8301L12.7197 13.7783L9.96484 11.0234C9.13519 11.6355 8.11012 11.998 7 11.998C4.23858 11.998 2 9.75947 2 6.99805C2 4.23662 4.23858 1.99805 7 1.99805ZM7 3.49805C5.067 3.49805 3.5 5.06505 3.5 6.99805C3.5 8.93104 5.067 10.498 7 10.498C8.933 10.498 10.5 8.93104 10.5 6.99805C10.5 5.06505 8.933 3.49805 7 3.49805Z" />
        </svg>
      </span>
      <ComboboxInput
        :id="row?.controlId"
        ref="input"
        class="setting-control setting-combobox__input"
        :class="{
          'is-initial-focus': initialFocusRingSuppressed,
          'is-pointer-focus': pointerFocus,
          'setting-combobox__input--with-icon': props.showSearchIcon,
        }"
        :model-value="props.searchTerm"
        :type="props.inputType"
        :inputmode="props.inputMode || (props.inputType === 'search' ? 'search' : undefined)"
        :display-value="committedLabel"
        :disabled="isDisabled"
        :placeholder="props.placeholder"
        :aria-label="props.ariaLabel"
        :aria-labelledby="props.ariaLabelledby || row?.labelId"
        :aria-describedby="props.ariaDescribedby || row?.describedBy?.value"
        :aria-busy="props.loading ? 'true' : undefined"
        @update:model-value="emit('update:searchTerm', $event)"
        @input="emit('search-intent')"
        @pointerdown="pointerFocus = true"
        @focus="handleFocus"
        @keydown="pointerFocus = false"
        @blur="handleBlur"
        @keydown.esc.stop.prevent="dismiss()"
      />
    </ComboboxAnchor>
    <ComboboxPortal :disabled="props.inlineResults">
      <ComboboxContent
        class="setting-popup setting-combobox__content"
        :class="{
          'setting-combobox__content--hide-indicator': !props.showSelectionIndicator,
          'setting-combobox__content--inline': props.inlineResults,
        }"
        position="popper"
        align="start"
        :side-offset="4"
        :collision-padding="12"
      >
        <ComboboxViewport class="setting-popup__viewport">
          <p v-if="props.loading" class="setting-popup__message" role="status">
            {{ props.loadingText }}
          </p>
          <template v-else-if="props.options.length">
            <ComboboxItem
              v-for="option in props.options"
              :key="optionKey(option)"
              class="setting-option"
              :value="props.getOptionValue(option)"
              :disabled="option.disabled"
            >
              <span class="setting-option__copy">
                <span>{{ props.getOptionLabel(option) }}</span>
                <span v-if="props.getOptionDescription(option)" class="setting-option__description">
                  {{ props.getOptionDescription(option) }}
                </span>
              </span>
              <ComboboxItemIndicator class="setting-option__indicator" aria-hidden="true">
                <svg viewBox="0 0 16 16" fill="none" focusable="false">
                  <path fill="currentColor" d="M4.2996 7.23968C4.01775 6.93614 3.5432 6.91857 3.23966 7.20042C2.93613 7.48227 2.91856 7.95682 3.20041 8.26035L6.45041 11.7603C6.7612 12.095 7.29647 12.0766 7.58346 11.7212L12.8335 5.22127C13.0937 4.89904 13.0435 4.42683 12.7213 4.16657C12.399 3.9063 11.9268 3.95654 11.6665 4.27877L6.96051 10.1053L4.2996 7.23968Z" />
                </svg>
              </ComboboxItemIndicator>
            </ComboboxItem>
          </template>
          <p v-else-if="!props.createActionLabel && !props.inlineResults" class="setting-popup__message" :role="props.emptyRole">
            {{ props.emptyText }}
          </p>
          <template v-if="!props.loading && props.createActionLabel">
            <ComboboxItem
              class="setting-option setting-option--create"
              :value="CREATE_VALUE"
            >
              <svg class="setting-option__leading-icon" viewBox="0 0 16 16" fill="none" aria-hidden="true" focusable="false">
                <path fill="currentColor" d="M8.75 4C8.75 3.58579 8.41421 3.25 8 3.25C7.58579 3.25 7.25 3.58579 7.25 4V7.25H4C3.58579 7.25 3.25 7.58579 3.25 8C3.25 8.41421 3.58579 8.75 4 8.75H7.25V12C7.25 12.4142 7.58579 12.75 8 12.75C8.41421 12.75 8.75 12.4142 8.75 12V8.75H12C12.4142 8.75 12.75 8.41421 12.75 8C12.75 7.58579 12.4142 7.25 12 7.25H8.75V4Z" />
              </svg>
              <span>{{ props.createActionLabel }}</span>
            </ComboboxItem>
          </template>
        </ComboboxViewport>
      </ComboboxContent>
    </ComboboxPortal>
    </ComboboxRoot>
    <p
      v-if="props.inlineResults && canOpen && !props.loading && !props.options.length && !props.createActionLabel"
      class="setting-popup setting-combobox__inline-message setting-popup__message"
      :role="props.emptyRole"
    >
      {{ props.emptyText }}
    </p>
  </div>
</template>
