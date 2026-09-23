# E1003 double-tap wake

On battery, the E1003 returns to deep sleep after 2 minutes without interaction.
The GT911 enters gesture mode with double-tap recognition only. USB retains the
existing always-awake behavior. Normal taps and swipes work while awake.
The double-tap interval is set to the controller's 1.5-second maximum while
preserving its factory gain setting. The green and white buttons remain the
visible wake controls when the e-ink image is unchanged during sleep.

A double tap wakes the device and applies the action at its last reported
position once, using the same hit testing as normal touch. Invalid or unavailable
gesture coordinates wake without selecting anything. The boot gesture is read
before resetting the GT911 back to coordinate mode. A held second tap is discarded
until release. Cached navigation does not connect Wi-Fi unless its data is due.

Only gesture masks and the gesture interrupt pulse setting change in the factory
configuration; its calibration and normal coordinate report rate are retained.
The checksum is recalculated and the changes are read back. INT stays high until
read, so an event during deep-sleep entry is not lost as a short pulse. The driver
checks for the gesture firmware's `GEST` signature before choosing active-high
wake. Unsupported gesture firmware or an I2C failure triggers a controller reset
and normal touch wake as fallback, when the controller remains available.

## Verification

Host tests exercise the actual register driver and dashboard hit testing:
calibration/checksum preservation, command order, gesture wake polarity, reset,
one-time coordinate consumption, invalid coordinates, unsupported firmware and
I2C failures. These tests do not measure current or prove hardware gesture support.

Before claiming battery savings, validate on an E1003 running on battery:

- Wait 2 minutes after interaction. Confirm `GT911 double-tap doze enabled`
  and `Entering deep sleep now`. A fallback warning means savings are unverified.
- Single tap/swipe while asleep: no wake. Double tap on a day, overview header
  and overview row: the intended action runs once. Repeat with the second tap held.
- Confirm ordinary taps/swipes after waking and repeated sleep/wake cycles.
- Verify timer refresh, physical buttons, USB insertion and low-battery recovery.
- Compare battery-side current in normal touch standby and gesture standby;
  measure complete refresh/wake cycles too. Record board/controller firmware,
  battery voltage, refresh frequency and false wakes over at least 24 hours.

Protocol references: [Goodix programming guide, sections 3.2, 4.4 and 7](https://www.lcd-module.de/fileadmin/eng/pdf/zubehoer/GT911_Programming_Guide_Rev.10.pdf),
[gesture pulse setting](https://www.orientdisplay.com/de/pdf/GT911.pdf),
[Goodix reference driver](https://github.com/goodix/gt9xx_driver_android/blob/master/gt9xx.c).
