import { BOARD_IDS } from '../src/config/configuration'
import { createEsptoolAdapter } from '../src/installer/esptoolAdapter'
import { requestInstallerPort } from '../src/installer/serialPortAdapter'

const button = document.querySelector('#flash')
const progress = document.querySelector('#progress')
const status = document.querySelector('#status')
const log = document.querySelector('#log')

function write(message) {
  log.textContent += `${message}\n`
  log.scrollTop = log.scrollHeight
}

button.addEventListener('click', async () => {
  button.disabled = true
  progress.value = 0
  log.textContent = ''
  let transport
  try {
    status.textContent = 'Loading local build…'
    const [metadataResponse, firmwareResponse] = await Promise.all([
      fetch('/__local-e1003/meta', { cache: 'no-store' }),
      fetch('/__local-e1003/app.bin', { cache: 'no-store' }),
    ])
    if (!metadataResponse.ok || !firmwareResponse.ok) throw new Error('Start the local E1003 tool first.')
    const metadata = await metadataResponse.json()
    const firmware = new Uint8Array(await firmwareResponse.arrayBuffer())
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', firmware)),
      byte => byte.toString(16).padStart(2, '0')).join('')
    if (firmware.length !== metadata.size || hash !== metadata.sha256) {
      throw new Error('The local build failed verification.')
    }
    status.textContent = 'Choose the E1003 USB device…'
    const port = await requestInstallerPort(navigator, BOARD_IDS.E1003)
    if (!port) { status.textContent = 'Cancelled.'; return }
    const esptool = createEsptoolAdapter()
    const identity = await esptool.identify(port, { write, writeLine: write })
    transport = identity.transport
    if (identity.chipFamily !== 'ESP32-S3') throw new Error('This device is not an E1003.')
    status.textContent = 'Writing firmware. Keep USB connected…'
    await esptool.flash({
      loader: identity.loader,
      transport,
      bundle: { eraseFlash: false, parts: [{ offset: 0x20000, data: firmware }] },
      onProgress: ({ written, total }) => {
        progress.value = Math.round(written / total * 100)
        status.textContent = `Writing firmware… ${progress.value}%`
      },
    })
    transport = null
    progress.value = 100
    status.textContent = 'Done. E1003 restarting.'
  } catch (error) {
    status.textContent = error?.message ?? 'Flash failed.'
    write(error?.cause?.message ?? error?.message ?? String(error))
  } finally {
    await transport?.disconnect().catch(() => {})
    button.disabled = false
  }
})
