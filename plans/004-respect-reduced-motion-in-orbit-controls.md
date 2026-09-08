# 004 — Remove camera inertia for reduced motion

- **Status**: TODO
- **Commit**: 85c6128
- **Severity**: MEDIUM
- **Category**: Accessibility & gestures
- **Estimated scope**: 3 files, small JavaScript change

## Problem

OrbitControls always applies momentum after the pointer stops:

```js
/* web/src/configurator/sceneController.js:65 — current */
export function configureOrbitControls(controls) {
  controls.enablePan = false
  controls.enableDamping = true
  controls.dampingFactor = 0.075
```

The demand-driven render loop is efficient and should remain. Only the post-gesture inertia needs to respect the user's preference.

## Target

```js
export function configureOrbitControls(controls, { reduceMotion = false } = {}) {
  controls.enablePan = false
  controls.enableDamping = !reduceMotion
  controls.dampingFactor = 0.075
  // existing limits and speeds unchanged
}
```

`WindpeekScene.vue` must read `(prefers-reduced-motion: reduce)` before configuring controls and respond if the preference changes while the page is open.

## Repo conventions to follow

`WindpeekScene.vue:424–443` centralizes setup cleanup. Register the media-query listener during initialization and remove it there. Keep rendering demand-driven through `requestRender()`.

## Steps

1. Extend `configureOrbitControls` in `sceneController.js` with the exact optional options argument shown above.
2. In `WindpeekScene.vue`, add a `reducedMotionQuery` module variable beside `resizeObserver`.
3. Before line 280 configures controls, call `window.matchMedia('(prefers-reduced-motion: reduce)')` and pass its current `matches` value.
4. Add a change listener that updates `controls.enableDamping`, calls `controls.update()`, and calls `requestRender()`.
5. Remove the listener in `onBeforeUnmount`.
6. Extend the scene-controller unit tests to cover the default and reduced-motion configurations. Add a component-level test for preference changes if the existing harness supports `matchMedia`.

## Boundaries

- Do NOT remove OrbitControls damping for the default experience.
- Do NOT change dampingFactor, rotateSpeed, zoomSpeed, camera limits, or hero pose.
- Do NOT replace the demand-driven rAF loop.
- Do NOT change MapLibre; its `easeTo` already respects reduced motion automatically.
- If the cited code has drifted since commit `85c6128`, STOP and report it.

## Verification

- **Mechanical**: run `npm test -- scene-controller`, then `npm test` and `npm run build` from `web/`.
- **Feel check**: normally, drag and release the product; it should settle with the existing gentle inertia. Under reduced motion, it should stop with the pointer while remaining fully draggable. Change the OS preference while the page is open and confirm behavior updates without reload.
- **Done when**: default physicality is unchanged and reduced-motion users get direct camera control without trailing motion.

