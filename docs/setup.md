# Install WindScout on a reTerminal

WindScout supports one spot per device. Configure the spot and display options in the browser, then select **Install** in the inspector.

## What you need

- A Seeed Studio reTerminal E1001, E1002 or E1003. Other ESP devices are not supported.
- A USB-C data cable. A charge-only cable will not work.
- Current Firefox, Chrome, or Edge on a desktop computer. Firefox 151 or newer is required.
- A 2.4 GHz Wi-Fi network and its password.

The installer is intentionally unavailable on phones. Mobile can preview display options, but creating spots and installing software stays on desktop.

## Installation

1. Connect your reTerminal directly to the desktop with USB-C.
2. Open the configurator over HTTPS, select your reTerminal model, choose one spot, and set the display options.
3. Select **Install**, then **Continue** to open the browser's USB device window.
4. Choose the connected reTerminal in the browser's USB device window.
5. If WindScout cannot verify the enclosure, compare it with the selected model's illustration and confirm the model. This confirmation is required before a clean flash.
6. Keep USB connected while firmware is being written.
7. After the device restarts, the installer reconnects automatically. Choose it again only if automatic reconnect cannot find it.
8. Choose Wi-Fi and enter the password. The browser sends it directly to the connected device and does not save it.

Success is shown only after the device confirms the configuration digest, Wi-Fi connection, and a rendered forecast.

## Updating an installed device

The installer chooses the safest route automatically:

- Matching firmware and setup: no write.
- Different spot or display options: configuration-only update.
- Older firmware: preserving update that does not erase Wi-Fi or user storage.
- Missing or damaged firmware: clean install or repair, with an explicit warning.

The owner never needs to choose a technical flash mode.
