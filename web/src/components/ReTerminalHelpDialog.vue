<script setup>
import {
  DialogClose,
  DialogContent,
  DialogDescription,
  DialogOverlay,
  DialogPortal,
  DialogRoot,
  DialogTitle,
} from 'reka-ui'
import ReTerminalComparison from './ReTerminalComparison.vue'

defineProps({
  open: { type: Boolean, required: true },
})

const emit = defineEmits(['update:open'])

</script>

<template>
  <DialogRoot :open="open" @update:open="emit('update:open', $event)">
    <DialogPortal>
      <DialogOverlay class="reterminal-help__overlay" />
      <DialogContent class="reterminal-help reterminal-device-help">
        <header>
          <DialogTitle class="reterminal-help__title">Windpeek for reTerminal</DialogTitle>
          <DialogDescription class="reterminal-help__description">
            Choose your reTerminal by screen size and colour. Windpeek only uses colour for the threshold line. Direct installation supports E1001, E1002 and E1003.
          </DialogDescription>
        </header>

        <ReTerminalComparison />

        <DialogClose as-child>
          <button class="reterminal-help__close" type="button" aria-label="Close reTerminal help">
            <svg viewBox="0 0 16 16" fill="none" aria-hidden="true">
              <path d="m4 4 8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" />
            </svg>
          </button>
        </DialogClose>
      </DialogContent>
    </DialogPortal>
  </DialogRoot>
</template>

<style scoped>
.reterminal-device-help { inline-size: min(31rem, calc(100% - 2rem)); }
</style>
