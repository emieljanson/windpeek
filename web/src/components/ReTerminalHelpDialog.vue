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
import { publicAssetUrl } from '../assets/publicAssetUrl'

defineProps({
  open: { type: Boolean, required: true },
})

const emit = defineEmits(['update:open'])

const devices = Object.freeze([
  Object.freeze({
    model: 'E1001',
    image: publicAssetUrl('devices/previews/e1001.png'),
    display: '7.5″ monochrome',
    price: '~$70',
    buyUrl: 'https://www.seeedstudio.com/reTerminal-E1001-p-6534.html?sensecap_affiliate=UF4PmgK&referring_service=link',
  }),
  Object.freeze({
    model: 'E1002',
    image: publicAssetUrl('devices/previews/e1002.png'),
    display: '7.3″ six-colour',
    price: '~$107',
    buyUrl: 'https://www.seeedstudio.com/reTerminal-E1002-p-6533.html?sensecap_affiliate=UF4PmgK&referring_service=link',
  }),
  Object.freeze({
    model: 'E1003',
    image: publicAssetUrl('devices/previews/e1003.png'),
    display: '10.3″ monochrome',
    price: '~$160',
    buyUrl: 'https://www.seeedstudio.com/reTerminal-E1003-p-6731.html?sensecap_affiliate=UF4PmgK&referring_service=link',
  }),
])
</script>

<template>
  <DialogRoot :open="open" @update:open="emit('update:open', $event)">
    <DialogPortal>
      <DialogOverlay class="reterminal-help__overlay" />
      <DialogContent class="reterminal-help">
        <header>
          <DialogTitle class="reterminal-help__title">Windpeek for reTerminal</DialogTitle>
          <DialogDescription class="reterminal-help__description">
            Choose your reTerminal by screen size and colour. Windpeek only uses colour for the threshold line. Direct installation supports E1001, E1002 and E1003.
          </DialogDescription>
        </header>

        <ul class="reterminal-help__devices">
          <li v-for="device in devices" :key="device.model" class="reterminal-help__device">
            <img class="reterminal-help__device-image" :src="device.image" alt="">
            <div class="reterminal-help__device-copy">
              <p><strong>{{ device.display }}</strong> — {{ device.model }}</p>
            </div>
            <a
              class="reterminal-help__buy"
              :href="device.buyUrl"
              :aria-label="`Buy reTerminal ${device.model}, approximately ${device.price.replace('~', '')}`"
              target="_blank"
              rel="sponsored noopener noreferrer"
            >
              Buy {{ device.price }}
            </a>
          </li>
        </ul>

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
