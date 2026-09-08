const rows = (...values) => values

export const steps = [
  {
    id: 'connect', title: 'Connect device', description: 'Plug the USB data cable into Windpeek.', motion: 'step in',
    pattern: rows('00011000', '00011000', '01111110', '01111110', '00011000', '00111100', '00100100', '00000000'),
  },
  {
    id: 'choose', title: 'Select device', description: 'Choose the connected reTerminal in the browser.', motion: 'target', loop: true,
    pattern: rows('11000011', '10000001', '00111100', '00100100', '00100100', '00111100', '10000001', '11000011'),
  },
  {
    id: 'check', title: 'Check device', description: 'Identify the board and choose the safest setup path.', motion: 'scan', loop: true,
    pattern: rows('00111100', '01000010', '01011010', '01011010', '01000010', '01011010', '01000010', '00111100'),
  },
  {
    id: 'confirm', title: 'Confirm E1002', description: 'Confirm an enclosure the browser cannot verify.', motion: 'assemble',
    pattern: rows('00111100', '01000010', '01011010', '01011010', '01000010', '01100110', '01000010', '00111100'),
  },
  {
    id: 'review', title: 'Review install', description: 'Show whether Windpeek will install, update or repair.', motion: 'drop',
    pattern: rows('00011000', '00011000', '01111110', '00111100', '00011000', '00000000', '01111110', '00111100'),
  },
  {
    id: 'prepare', title: 'Prepare firmware', description: 'Download and verify the release before writing.', motion: 'sort', loop: true,
    pattern: rows('11000000', '11001100', '00001100', '00110000', '00110011', '00000011', '01100110', '01100110'),
  },
  {
    id: 'write', title: 'Write firmware', description: 'Fill the device memory with verified firmware.', motion: 'fill', loop: true,
    pattern: rows('00111100', '01111110', '11011011', '11111111', '11011011', '11111111', '01111110', '00111100'),
  },
  {
    id: 'reconnect', title: 'Reconnect', description: 'Select Windpeek again after its restart.', motion: 'join', loop: true,
    pattern: rows('00000000', '11000011', '11100111', '01111110', '01111110', '11100111', '11000011', '00000000'),
  },
  {
    id: 'wifi', title: 'Choose Wi-Fi', description: 'Give Windpeek a network for forecast updates.', motion: 'radiate',
    pattern: rows('00000000', '01111110', '10000001', '00111100', '01000010', '00011000', '00011000', '00000000'),
  },
  {
    id: 'apply', title: 'Apply setup', description: 'Transfer the selected spot and display settings.', motion: 'toggle', loop: true,
    pattern: rows('00000000', '01111110', '00011000', '01111110', '00110000', '01111110', '00001100', '00000000'),
  },
  {
    id: 'verify', title: 'Verify forecast', description: 'Confirm Wi-Fi, configuration and the first render.', motion: 'sweep', loop: true,
    pattern: rows('00000010', '00100110', '01101100', '11011000', '01101100', '00100110', '00000010', '00000000'),
  },
  {
    id: 'complete', title: 'Done', description: 'The chosen setup is live on the device.', motion: 'draw once',
    pattern: rows('00000000', '00000001', '00000011', '01000110', '01101100', '00111000', '00010000', '00000000'),
  },
  {
    id: 'error', title: 'Could not continue', description: 'A clear terminal state without constant alarm motion.', motion: 'one shake', error: true,
    pattern: rows('00000000', '11000011', '01100110', '00111100', '00111100', '01100110', '11000011', '00000000'),
  },
]

const toCells = (pattern) => pattern.flatMap((row, y) => [...row].map((value, x) => value === '1' ? y * 8 + x : -1)).filter((index) => index >= 0)
const toIndex = (x, y) => (x >= 0 && x < 8 && y >= 0 && y < 8 ? y * 8 + x : -1)

function shifted(cells, dx = 0, dy = 0) {
  return cells.map((index) => toIndex((index % 8) + dx, Math.floor(index / 8) + dy)).filter((index) => index >= 0)
}

function splitShifted(cells, amount) {
  return cells.map((index) => {
    const x = index % 8
    const y = Math.floor(index / 8)
    return toIndex(x + (x < 4 ? -amount : amount), y)
  }).filter((index) => index >= 0)
}

function revealed(cells, ratio, order = 'reading') {
  const sorted = [...cells].sort((a, b) => {
    if (order === 'center') {
      const da = Math.abs((a % 8) - 3.5) + Math.abs(Math.floor(a / 8) - 3.5)
      const db = Math.abs((b % 8) - 3.5) + Math.abs(Math.floor(b / 8) - 3.5)
      return da - db
    }
    if (order === 'bottom') return Math.floor(b / 8) - Math.floor(a / 8) || (a % 8) - (b % 8)
    return a - b
  })
  return sorted.slice(0, Math.ceil(sorted.length * ratio))
}

function scanFrame(cells, row) {
  return [...new Set([...cells, ...Array.from({ length: 8 }, (_, x) => row * 8 + x)])]
}

function framesFor(step, flavor) {
  if (step.frames) return step.frames.map(toCells)
  const base = toCells(step.pattern)
  const soft = flavor === 'dot'
  const stepsIn = soft ? 5 : 3

  switch (step.motion) {
    case 'step in': return Array.from({ length: stepsIn }, (_, i) => shifted(base, stepsIn - i - 1, 0))
    case 'target': return [base, shifted(base, 0, 1), base, shifted(base, 0, -1)]
    case 'scan': return Array.from({ length: 8 }, (_, row) => scanFrame(base, row))
    case 'drop': return [shifted(base, 0, -2), shifted(base, 0, -1), base]
    case 'sort': return [0.2, 0.4, 0.6, 0.8, 1].map((ratio) => revealed(base, ratio, 'center'))
    case 'fill': return [0.18, 0.34, 0.5, 0.68, 0.84, 1].map((ratio) => revealed(base, ratio, 'bottom'))
    case 'join': return [splitShifted(base, 2), splitShifted(base, 1), base]
    case 'radiate': return [0.25, 0.5, 0.75, 1].map((ratio) => revealed(base, ratio, 'bottom'))
    case 'toggle': return [base, shifted(base, 1, 0), base, shifted(base, -1, 0)]
    case 'sweep': return [0, 1, 2, 3, 4, 5, 6, 7].map((row) => scanFrame(base, row))
    case 'draw once': return [0.2, 0.4, 0.6, 0.8, 1].map((ratio) => revealed(base, ratio, 'reading'))
    case 'one shake': return [base, shifted(base, -1, 0), shifted(base, 1, 0), base]
    default: return [0.25, 0.5, 0.75, 1].map((ratio) => revealed(base, ratio, 'center'))
  }
}

function iconMarkup(step) {
  const cells = Array.from({ length: 64 }, (_, index) => `<span class="matrix-cell" data-cell="${index}" aria-hidden="true"></span>`).join('')
  return `<div class="matrix-icon" data-icon="${step.id}" role="img" aria-label="${step.title} icon">${cells}</div>`
}

export function studyMarkup(config) {
  const cards = steps.map((baseStep, index) => {
    const step = { ...baseStep, ...(config.stepOverrides?.[baseStep.id] ?? {}) }
    return `
    <article class="step-card${step.error ? ' step-card--error' : ''}">
      <span class="motion-label">${step.motion}</span>
      <div class="icon-stage">${iconMarkup(step)}</div>
      <div class="step-copy">
        <p class="step-number">${String(index + 1).padStart(2, '0')} · ${step.id}</p>
        <h2>${step.title}</h2>
        <p>${step.description}</p>
      </div>
    </article>
  `
  }).join('')

  return `
    <section class="study-shell variant-${config.familyClass ?? config.className} variant-${config.className}">
      <header class="study-header">
        <div>
          <p class="study-kicker">Windpeek wizard icons · ${config.name}</p>
          <h1 class="study-title">${config.headline}</h1>
        </div>
        <div>
          <p class="study-intro">${config.description}</p>
          <p class="study-note"><kbd>1–3</kbd> switch direction <kbd>R</kbd> replay motion</p>
        </div>
      </header>
      <div class="step-grid">${cards}</div>
    </section>
  `
}

export function startAnimations(root, config) {
  const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches
  const timers = []

  root.querySelectorAll('[data-icon]').forEach((icon) => {
    const baseStep = steps.find((candidate) => candidate.id === icon.dataset.icon)
    const step = { ...baseStep, ...(config.stepOverrides?.[baseStep.id] ?? {}) }
    const frames = framesFor(step, config.flavor)
    const cells = [...icon.querySelectorAll('.matrix-cell')]
    let frameIndex = reduceMotion ? frames.length - 1 : 0

    const paint = () => {
      const active = new Set(frames[frameIndex])
      cells.forEach((cell, index) => cell.classList.toggle('is-active', active.has(index)))
    }

    paint()
    if (reduceMotion || frames.length === 1) return

    const tick = () => {
      if (!step.loop && frameIndex === frames.length - 1) return
      frameIndex = (frameIndex + 1) % frames.length
      paint()
    }

    timers.push(window.setInterval(tick, config.frameDuration))
  })

  return () => timers.forEach((timer) => window.clearInterval(timer))
}
