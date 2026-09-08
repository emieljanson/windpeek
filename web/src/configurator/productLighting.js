export const PRODUCT_LIGHTING = Object.freeze({
  background: 0xf3f5f7,
  hemisphere: Object.freeze({ sky: 0xf7f8fb, ground: 0x68717c, intensity: 0.32 }),
  key: Object.freeze({
    kind: 'spot',
    color: 0xfff8ee,
    intensity: 0.8,
    position: Object.freeze([-0.075, 0.34, 0.2]),
    angle: 0.82,
    penumbra: 0.92,
    decay: 1.35,
    distance: 1.8,
  }),
  softbox: Object.freeze({
    color: 0xfffcf4,
    intensity: 2.9,
    width: 0.46,
    height: 0.28,
    position: [0.3, 0.12, 0.43],
  }),
  accent: Object.freeze({
    color: 0xd9edf0,
    intensity: 1.05,
    width: 0.075,
    height: 0.48,
    position: [0.27, 0.06, 0.34],
  }),
  rim: Object.freeze({ color: 0xc9dcde, intensity: 0.28, position: [0.34, 0.08, -0.2] }),
  environment: Object.freeze({
    background: 0xbdc4ca,
    key: 0xfffcf5,
    top: 0xf5f7fb,
    rim: 0xd9e0e8,
    rimWidth: 0.18,
    backdrop: 0xd2d7dc,
  }),
})

// A lit product in a dark studio: the display remains a reflective material.
export const DARK_PRODUCT_LIGHTING = {
  ...PRODUCT_LIGHTING,
  background: 0x101012,
  hemisphere: { sky: 0xb9b9bb, ground: 0x28282a, intensity: 0.035 },
  key: { ...PRODUCT_LIGHTING.key, color: 0xffffff, intensity: 0.2058, position: [0, 0.16, 0.16], target: [0, 0.025, 0], angle: 0.55, penumbra: 1 },
  softbox: { ...PRODUCT_LIGHTING.softbox, color: 0xffffff, intensity: 0.4, width: 0.20, height: 0.18, position: [0, 0.25, 0.35] },
  accent: { ...PRODUCT_LIGHTING.accent, color: 0xfafafc, intensity: 2.8, width: 0.10, height: 0.12, position: [0, 0.14, -0.16] },
  rim: { ...PRODUCT_LIGHTING.rim, color: 0xe7e7e9, intensity: 0.22, position: [0.34, 0.10, -0.06] },
  environment: {
    background: 0x0c0c0e, key: 0x555557, top: 0x49494b,
    rim: 0x727274, rimWidth: 0.18, backdrop: 0x0c0c0e, floor: 0x151517,
  },
}
