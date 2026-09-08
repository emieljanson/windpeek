import { createActualScene } from './actual-scene.js'

const variants = [
  { kicker: 'Light replaces material', title: 'Light Core', description: 'The physical cable recedes until only a compact energy core remains.' },
  { kicker: 'ThreeUI idea, cable-bound', title: 'Matrix Junction', description: 'Precise signal branches grow out of the real cable curve.' },
  { kicker: 'Light wraps material', title: 'Field Sheath', description: 'A moving optical field hugs the existing cable surface.' },
]

const picker = document.querySelector('.proto-picker')
const highlight = picker.querySelector('.proto-picker-highlight')
const items = [...picker.querySelectorAll('.proto-picker-item:not(.proto-picker-replay)')]
const replay = picker.querySelector('.proto-picker-replay')
const loading = document.querySelector('#loading-state')
const kicker = document.querySelector('#variant-kicker')
const title = document.querySelector('#variant-title')
const description = document.querySelector('#variant-description')
let current = 0
let sceneController

function moveHighlight() {
  const element = items[current]
  highlight.style.width = `${element.offsetWidth}px`
  highlight.style.transform = `translateX(${element.offsetLeft}px)`
}

function setActive(index) {
  if (index < 0 || index >= variants.length) return
  current = index
  items.forEach((element, itemIndex) => {
    element.toggleAttribute('data-active', itemIndex === index)
    if (itemIndex === index) element.setAttribute('aria-current', 'true')
    else element.removeAttribute('aria-current')
  })
  kicker.textContent = variants[index].kicker
  title.textContent = variants[index].title
  description.textContent = variants[index].description
  moveHighlight()
  const url = new URL(location)
  url.searchParams.set('v', index + 1)
  history.replaceState(null, '', url)
  sceneController?.setVariant(index)
}

items.forEach((element, index) => element.addEventListener('click', () => setActive(index)))
replay.addEventListener('click', () => sceneController?.replay())
window.addEventListener('resize', moveHighlight)

document.addEventListener('keydown', (event) => {
  if (/^(INPUT|TEXTAREA|SELECT)$/.test(event.target.tagName) || event.target.isContentEditable) return
  if (event.metaKey || event.ctrlKey || event.altKey) return
  const number = Number.parseInt(event.key, 10)
  if (number >= 1 && number <= variants.length) setActive(number - 1)
  else if (event.key === 'ArrowRight') setActive((current + 1) % variants.length)
  else if (event.key === 'ArrowLeft') setActive((current - 1 + variants.length) % variants.length)
  else if (event.key === 'r' || event.key === 'R') sceneController?.replay()
})

const initial = Math.min(variants.length - 1, Math.max(0, (Number.parseInt(new URLSearchParams(location.search).get('v'), 10) || 1) - 1))
setActive(initial)

try {
  sceneController = await createActualScene({
    canvas: document.querySelector('#scene-canvas'),
    onReady: () => loading.setAttribute('data-hidden', ''),
  })
  sceneController.setVariant(initial)
} catch (error) {
  loading.textContent = error instanceof Error ? error.message : 'The Windpeek scene could not load.'
}

requestAnimationFrame(() => requestAnimationFrame(() => picker.setAttribute('data-ready', '')))
