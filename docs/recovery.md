# Windpeek USB recovery

The installer always states whether it is safe to disconnect USB.

## Device is not listed

1. Use current Firefox, Chrome or Edge on desktop. Firefox 151 or newer is required.
2. Check that the page uses HTTPS or localhost.
3. Try a known USB data cable and a direct computer port rather than a hub.
4. Disconnect the E1002, reconnect it, and select **Connect device** again.

Cancelling the browser's device window changes nothing and can be retried safely.

## Setup stopped during Wi-Fi or configuration

Firmware remains bootable because configuration and credentials are committed together only after validation. Retry the Wi-Fi step; the password field is cleared after a failed attempt. A configuration retry does not rewrite firmware.

## USB disconnected during firmware writing

Do not assume the device can boot. Reconnect USB and start **Repair Windpeek**. If the browser cannot detect the device automatically, put the E1002 into its documented ESP32-S3 download/bootloader mode, then choose it again. A repair uses the verified clean-install bundle.

## Wrong device selected

Windpeek blocks known E1001 and non-ESP32-S3 devices. A generic ESP32-S3 cannot prove which enclosure it is in, so no destructive write is available until the owner explicitly confirms an E1002.

The existing captive-portal recovery path remains enabled until the physical USB acceptance matrix is complete.
