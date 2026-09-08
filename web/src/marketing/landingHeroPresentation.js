export const LANDING_HERO_PRESENTATION = Object.freeze({
  width: 1672,
  height: 941,
  image: 'marketing/windpeek-hero-yellow-v17-2x.png',
  imageFallback: 'marketing/windpeek-hero-yellow-v17-1672w.jpg',
  imageWebp: 'marketing/windpeek-hero-yellow-v17-2x.webp',
  responsiveWebp: Object.freeze([
    { width: 960, image: 'marketing/windpeek-hero-yellow-v17-960w.webp' },
    { width: 1672, image: 'marketing/windpeek-hero-yellow-v17-1672w.webp' },
    { width: 2508, image: 'marketing/windpeek-hero-yellow-v17-2508w.webp' },
  ]),
  framing: Object.freeze({
    zoom: 1.2,
    focusX: 48.5,
    focusY: 50,
  }),
  corners: Object.freeze([
    Object.freeze({ x: 608.2, y: 285.5 }),
    Object.freeze({ x: 1229.6, y: 279.7 }),
    Object.freeze({ x: 1232.7, y: 657.8 }),
    Object.freeze({ x: 604.2, y: 652.5 }),
  ]),
  finish: Object.freeze({
    opacity: 1,
    brightness: 1.18,
    contrast: 1,
    shadowSize: 0,
    shadowOpacity: 0.26,
    reflection: 0.1,
    reflectionColor: '#ffe0c2',
  }),
})
