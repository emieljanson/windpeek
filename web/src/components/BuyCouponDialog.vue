<script setup>
import { ref } from 'vue'
import '../styles/reterminal-help-dialog.css'
import '../styles/settings-controls.css'
import { DialogRoot, DialogTrigger, DialogPortal, DialogOverlay, DialogContent, DialogTitle, DialogDescription, DialogClose } from 'reka-ui'

const props = defineProps({ device: { type: Object, required: true } })
const copyFailed = ref(false)
const copied = ref(false)
const busy = ref(false)
const codeText = ref(null)
const coupon = () => props.device.model === 'E1003' ? 'G8CLJUXJ' : '796ICGWL'

async function copyCode() {
  copyFailed.value = false
  if (busy.value) return
  busy.value = true
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(coupon())
    } else {
      const selection = window.getSelection()
      const previousRanges = Array.from({ length: selection.rangeCount }, (_, index) => selection.getRangeAt(index).cloneRange())
      const range = document.createRange()
      range.selectNodeContents(codeText.value)
      selection.removeAllRanges()
      selection.addRange(range)
      let success
      try {
        success = document.execCommand('copy')
      } finally {
        selection.removeAllRanges()
        previousRanges.forEach(previousRange => selection.addRange(previousRange))
      }
      if (!success) throw new Error('Copy unavailable')
    }
    copied.value = true
  } catch {
    copyFailed.value = true
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <DialogRoot @update:open="copyFailed = false; copied = false">
    <DialogTrigger as-child>
      <button class="button hardware-model__buy" type="button" :aria-label="`Buy reTerminal ${device.model}, approximately ${device.price.replace('~', '')}`">Buy for {{ device.price }}</button>
    </DialogTrigger>
    <DialogPortal>
      <DialogOverlay class="reterminal-help__overlay" />
      <DialogContent class="reterminal-help coupon-dialog">
        <header>
          <DialogTitle class="reterminal-help__title">Get 5% off your {{ device.model }}</DialogTitle>
          <DialogDescription class="reterminal-help__description">{{ device.model === 'E1003' ? 'The listed price includes this discount. Apply the code at checkout.' : 'Apply this code at checkout for 5% off.' }} Buying through this link supports Windpeek.</DialogDescription>
        </header>
        <div class="coupon-dialog__field">
          <span ref="codeText" class="coupon-dialog__code">{{ coupon() }}</span>
          <button type="button" :disabled="busy" @click="copyCode">{{ copied ? 'Copied' : 'Copy' }}</button>
        </div>
        <p v-if="copyFailed" role="status" class="reterminal-help__description">Unable to copy. Select and copy the code manually.</p>
        <span class="coupon-dialog__status" role="status">{{ copied ? 'Code copied' : '' }}</span>
        <a class="coupon-dialog__continue" :href="device.buyUrl" rel="sponsored">Continue to store</a>
        <DialogClose class="reterminal-help__close" aria-label="Close discount dialog">
          <svg viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m4 4 8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" /></svg>
        </DialogClose>
      </DialogContent>
    </DialogPortal>
  </DialogRoot>
</template>

<style>
.reterminal-help.coupon-dialog { inline-size: min(25rem, calc(100% - 2rem)); box-sizing: border-box; color: var(--settings-control-ink, #171817); background: var(--panel-background, #fff); font-family: 'Inter Variable', Inter, sans-serif; }
.coupon-dialog__field { display: flex; align-items: center; gap: 4px; padding: 4px; border-radius: var(--settings-control-radius); background: var(--settings-control-surface); }
.coupon-dialog__code { min-width: 0; flex: 1; padding: 0 8px; user-select: text; -webkit-user-select: text; }
.coupon-dialog__field button { flex: 0 0 72px; height: 36px; border: 0; border-radius: 6px; padding: 0 10px; font: inherit; font-weight: 500; color: inherit; background: transparent; cursor: pointer; transition: background-color 120ms ease-out; }
.coupon-dialog__field button:hover { background: var(--settings-strong-surface); }
.coupon-dialog__field button:active { scale: 0.96; }
.coupon-dialog__field button:disabled { opacity: 0.5; cursor: wait; }
.coupon-dialog__continue { display: block; padding: 12px; border-radius: 10px; background: var(--panel-primary-background, #171817); color: var(--panel-primary-foreground, #fff); text-align: center; text-decoration: none; font-size: 14px; font-weight: 500; }
.coupon-dialog__status { position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%); }
.coupon-dialog :is(button, a):focus-visible { outline: 0; box-shadow: inset 0 0 0 1px var(--settings-focus); }
@media (prefers-color-scheme: dark) { .reterminal-help.coupon-dialog { color: var(--settings-control-ink, #eee); background: var(--panel-background, #252525); } }
</style>
