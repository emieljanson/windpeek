# 008 — Connect Minimum wind to its toggle

- **Status**: TODO
- **Commit**: 85c6128
- **Severity**: LOW
- **Category**: Missed opportunity
- **Estimated scope**: 3 files, medium Vue/CSS/test change

## Problem

The dependent control is inserted instantly:

```vue
<!-- web/src/components/WindpeekSettings.vue:149 — current -->
<SettingRow v-if="showThreshold" class="setting-row--compact-control" label="Minimum wind">
  <SettingNumberInput ... />
</SettingRow>
```

The sudden appearance does not visually explain that Minimum wind belongs to Wind threshold. However, keyboard interaction must remain instant and the surrounding form must not undergo an animated layout shift.

## Target

- Pointer-triggered entry only: opacity 0 and `translateY(-2px)` to rest over `120ms` using `var(--ease-out)`.
- Keyboard-initiated and programmatic changes: instant.
- Removal: instant; the system response should snap and neighboring rows should not wait for an exit.
- Reduced motion: instant.

```css
@media (prefers-reduced-motion: no-preference) {
  .minimum-wind-enter-active {
    transition: opacity var(--motion-fast) var(--ease-out),
      transform var(--motion-fast) var(--ease-out);
  }
  .minimum-wind-enter-from {
    opacity: 0;
    transform: translateY(-2px);
  }
}
```

## Repo conventions to follow

Use Vue's built-in `<Transition>`; the app has no motion dependency. Input modality is already tracked locally in the settings controls through pointerdown/keydown events.

## Steps

1. Import `ref` in `WindpeekSettings.vue` and add `const animateMinimumWind = ref(false)`.
2. On the `SettingRow` containing Wind threshold, add `@pointerdown="animateMinimumWind = true"` and `@keydown="animateMinimumWind = false"`. These events bubble from the actual switch.
3. Wrap the conditional Minimum wind row in `<Transition :name="animateMinimumWind ? 'minimum-wind' : undefined" @after-enter="animateMinimumWind = false">`.
4. Add the target enter-only CSS to `settings-controls.css`. Define no leave-active rule; removal must be immediate.
5. Ensure programmatic store changes cannot inherit stale pointer state. Reset the flag after entry and when threshold is turned off.
6. Extend `web/tests/settings.test.js` to verify the row still mounts/unmounts and preserves its value. Extend the e2e test to confirm keyboard toggling is immediate and pointer toggling receives the enter classes.

## Boundaries

- Do NOT animate height, grid rows, margin, padding, or the position of neighboring controls.
- Do NOT animate keyboard-triggered changes.
- Do NOT delay removal.
- Do NOT move focus automatically into Minimum wind.
- Do NOT change threshold state or validation.
- Requires plans 001 and 003.
- If Vue removes the enter classes before the e2e assertion can observe them, verify with DevTools or a unit transition stub rather than increasing the duration.

## Verification

- **Mechanical**: run `npm test -- settings.test.js`, `npm test`, `npm run build`, and the threshold section of `tests/e2e/configurator.spec.js`.
- **Feel check**: click Show for Wind threshold. Minimum wind should appear with a barely perceptible 2px settling motion; surrounding rows should not slide. Turn it off and it should disappear immediately. Repeat with keyboard: both directions must be instant. Under reduced motion, all paths must be instant.
- **Done when**: the pointer path explains the relationship without producing layout animation, while keyboard and reduced-motion paths remain direct.

