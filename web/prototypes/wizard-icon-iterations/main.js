import { stepConcepts } from './concepts.js'

const SIZE = 9
const FRAME_DURATION = 150

const steps = [
  { id: 'connect', title: 'Connect device', note: 'Plugs, docking and a data line that comes alive.' },
  { id: 'select', title: 'Select device', note: 'A moving target searches around one stable device.' },
  { id: 'confirm', title: 'Confirm device', note: 'The same target wobbles, then locks with equal spacing.' },
  { id: 'write', title: 'Write firmware', note: 'Ten different loaders: literal, systematic and playful.' },
  { id: 'reconnect', title: 'Reconnect', note: 'The broken relation visibly returns or repairs itself.' },
  { id: 'wifi', title: 'Choose Wi-Fi', note: 'Recognisable Wi-Fi, explored with ten pixel constructions.' },
  { id: 'apply', title: 'Apply setup', note: 'Settings organise, stream, snap and lock into place.' },
  { id: 'complete', title: 'Done', note: 'Flags, vanes and wind become the Windpeek success moment.' },
  { id: 'error', title: 'Error', note: 'Ten deliberately static crosses with different weight.' },
]

const studies = document.getElementById('studies')
const summary = document.getElementById('selection-summary')
const motionToggle = document.getElementById('motion-toggle')
const clearChoices = document.getElementById('clear-choices')
const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches
const storageKey = 'windpeek-9x9-semantic-icon-choices-v2'
let selections = JSON.parse(window.localStorage.getItem(storageKey) || '{}')
let motionPaused = reduceMotion
let frameIndex = 0
let timer
const renderedIcons = []

function iconMarkup(step, concept) {
  const cells = Array.from({ length: SIZE * SIZE }, (_, index) => `<span class="matrix-cell" data-cell="${index}" aria-hidden="true"></span>`).join('')
  return `<div class="matrix-icon" role="img" aria-label="${step.title}: ${concept.name}">${cells}</div>`
}

function renderStudies() {
  studies.innerHTML = steps.map((step, stepIndex) => {
    const concepts = stepConcepts[step.id]
    return `
      <section class="state-study" data-step="${step.id}">
        <header class="state-heading">
          <div>
            <p class="step-index">${String(stepIndex + 1).padStart(2, '0')}</p>
            <h2>${step.title}</h2>
          </div>
          <p>${step.note}</p>
        </header>
        <div class="iteration-grid">
          ${concepts.map((item, conceptIndex) => `
            <button
              class="iteration-card"
              type="button"
              data-concept="${conceptIndex}"
              aria-pressed="${selections[step.id] === conceptIndex}"
              title="${item.idea}"
            >
              ${iconMarkup(step, item)}
              <span class="iteration-label"><b>${String(conceptIndex + 1).padStart(2, '0')}</b>${item.name}</span>
            </button>
          `).join('')}
        </div>
      </section>
    `
  }).join('')

  renderedIcons.length = 0
  steps.forEach((step) => {
    studies.querySelectorAll(`[data-step="${step.id}"] .iteration-card`).forEach((card, conceptIndex) => {
      const item = stepConcepts[step.id][conceptIndex]
      renderedIcons.push({ frames: item.frames, preview: item.preview, cells: [...card.querySelectorAll('.matrix-cell')] })
      card.addEventListener('click', () => selectConcept(step.id, conceptIndex))
    })
  })
  paint()
  updateSummary()
}

function paint() {
  renderedIcons.forEach(({ frames, preview, cells }) => {
    const active = motionPaused ? preview : frames[frameIndex % frames.length]
    cells.forEach((cell, index) => cell.classList.toggle('is-active', active.has(index)))
  })
}

function selectConcept(stepId, conceptIndex) {
  selections[stepId] = conceptIndex
  window.localStorage.setItem(storageKey, JSON.stringify(selections))
  studies.querySelectorAll(`[data-step="${stepId}"] .iteration-card`).forEach((card, index) => {
    card.setAttribute('aria-pressed', String(index === conceptIndex))
  })
  updateSummary()
}

function updateSummary() {
  const choices = steps.filter((step) => selections[step.id] !== undefined)
  summary.innerHTML = choices.length
    ? choices.map((step) => `<span><strong>${step.title}</strong> ${stepConcepts[step.id][selections[step.id]].name}</span>`).join('<span aria-hidden="true">·</span>')
    : '<span>No concepts selected yet.</span>'
}

function restartTimer() {
  window.clearInterval(timer)
  if (motionPaused) return
  timer = window.setInterval(() => {
    frameIndex += 1
    paint()
  }, FRAME_DURATION)
}

motionToggle.addEventListener('click', () => {
  motionPaused = !motionPaused
  motionToggle.textContent = motionPaused ? 'Play motion' : 'Pause motion'
  frameIndex = 0
  paint()
  restartTimer()
})

clearChoices.addEventListener('click', () => {
  selections = {}
  window.localStorage.removeItem(storageKey)
  studies.querySelectorAll('.iteration-card').forEach((card) => card.setAttribute('aria-pressed', 'false'))
  updateSummary()
})

if (reduceMotion) motionToggle.textContent = 'Play motion'
renderStudies()
restartTimer()
