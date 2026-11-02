# E1003 button-only wake

On battery, the E1003 returns to deep sleep after 2 minutes without interaction.
The GT911 is held in reset through sleep. Touch cannot wake the device; press a
physical button first, then use ordinary taps and swipes. The button keeps its
existing navigation action. USB keeps the device awake, but touch still needs a
button press after a cold boot or a timer wake.

Scheduled forecast refreshes keep touch disabled. Button wakes release RESET and
initialize the controller; stale contacts are cleared before coordinate input.
No gesture configuration is written and there is no normal-touch wake fallback.
The RESET hold is applied even when the controller cannot be detected or the
battery is empty. Only the existing physical-button and timer wake sources remain.

## Verification

Host tests exercise the actual register driver: RESET low and held during sleep,
no coordinate input while disabled, both I2C addresses, button activation, stale
contact clearing, ordinary coordinates and releases, and missing-controller sleep.
Firmware builds and power-policy checks cover button/timer wake configuration.
These checks do not measure battery current or establish the cause of past drain.

Validate on an E1003 running on battery:

- Press a button; verify its navigation action and ordinary taps/swipes.
- Wait 2 minutes without interaction. Confirm `GT911 held in reset; button wake
  only` and `Entering deep sleep now`. Single/double taps and swipes must not wake.
- Repeat button/sleep cycles and verify scheduled forecast refreshes.
- Insert USB during a timer wake; verify touch stays off until a button press.
- Verify low-battery sleep and USB recovery.
- Measure battery-side sleep current and complete refresh cycles before/after.
  Record battery voltage, refresh frequency and runtime over at least 24 hours.

RESET is held low; the board has no separately switched touch power rail. Actual
current savings require hardware measurement.
