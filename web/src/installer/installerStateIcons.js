const GRID_SIZE = 9

const point = (x, y) => [[x, y]]
const horizontal = (y, from, to) => Array.from({ length: to - from + 1 }, (_, index) => [from + index, y])
const vertical = (x, from, to) => Array.from({ length: to - from + 1 }, (_, index) => [x, from + index])

function rectangle(x, y, width, height, filled = false) {
  const cells = []
  for (let row = y; row < y + height; row += 1) {
    for (let column = x; column < x + width; column += 1) {
      if (filled || row === y || row === y + height - 1 || column === x || column === x + width - 1) {
        cells.push([column, row])
      }
    }
  }
  return cells
}

const move = (shape, x, y) => shape.map(([column, row]) => [column + x, row + y])

function frame(...shapes) {
  const active = new Set(shapes.flat().filter(([x, y]) => (
    x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE
  )).map(([x, y]) => (y * GRID_SIZE) + x))

  return Array.from({ length: GRID_SIZE }, (_, y) => (
    Array.from({ length: GRID_SIZE }, (_, x) => active.has((y * GRID_SIZE) + x) ? '1' : '0').join('')
  ))
}

function target(radius = 3, length = 2) {
  const left = 4 - radius
  const right = 4 + radius
  const top = 4 - radius
  const bottom = 4 + radius
  return [
    ...horizontal(top, left, left + length - 1), ...vertical(left, top, top + length - 1),
    ...horizontal(top, right - length + 1, right), ...vertical(right, top, top + length - 1),
    ...horizontal(bottom, left, left + length - 1), ...vertical(left, bottom - length + 1, bottom),
    ...horizontal(bottom, right - length + 1, right), ...vertical(right, bottom - length + 1, bottom),
  ]
}

const connectorBlocks = [
  ...rectangle(0, 3, 2, 3, true),
  ...rectangle(7, 3, 2, 3, true),
]

const connectFrames = [2, 3, 4, 5, 6, 5, 4, 3].map((x) => frame(
  connectorBlocks,
  point(x, 3),
  point(8 - x, 5),
))

const openDevice = rectangle(3, 3, 3, 3)
const selectOrbitRoute = [
  [-1, -1], [-1, 0], [-1, 1], [0, 1],
  [1, 1], [1, 0], [1, -1], [0, -1],
]
const selectFrames = selectOrbitRoute.map(([x, y]) => frame(
  openDevice,
  move(target(3, 2), x, y),
))
const confirmedDevice = frame(openDevice, target(3, 2))

// Every packet keeps its column and advances exactly one row per frame.
// The varied starting rows make the rain feel loose without making it teleport.
const rainTrajectories = [
  [1, 0], [1, 4],
  [2, 2], [2, 6],
  [3, 1], [3, 5],
  [4, 3], [4, 7],
  [5, 0], [5, 5],
  [6, 2], [6, 7],
  [7, 1], [7, 5],
]
const firmwareFrames = Array.from({ length: 8 }, (_, step) => frame(
  ...rainTrajectories.map(([x, startingRow]) => point(x, (startingRow + step) % 8)),
  horizontal(8, 1, 7),
))

const wifi = frame(
  horizontal(1, 3, 5), point(2, 2), point(6, 2),
  point(1, 3), point(7, 3), horizontal(4, 3, 5),
  point(2, 5), point(6, 5), point(4, 7),
)

const wifiPulseFrames = [
  frame(point(4, 7)),
  frame(point(4, 7), horizontal(5, 3, 5)),
  frame(point(4, 7), horizontal(5, 3, 5), horizontal(3, 2, 6)),
  wifi,
]

const flagPole = vertical(2, 1, 7)
const completeFrames = [
  ['000000000', '001100000', '001011110', '001000010', '001000010', '001100010', '001011110', '001000000', '000000000'],
  ['000000000', '001110000', '001001110', '001000010', '001000010', '001110010', '001001110', '001000000', '000000000'],
  ['000000000', '001111000', '001000110', '001000010', '001000010', '001111010', '001000110', '001000000', '000000000'],
  ['000000000', '001111110', '001000010', '001000010', '001000010', '001111110', '001000000', '001000000', '000000000'],
  ['000000000', '001111000', '001000110', '001000010', '001000010', '001111010', '001000110', '001000000', '000000000'],
  ['000000000', '001110000', '001001110', '001000010', '001000010', '001110010', '001001110', '001000000', '000000000'],
]

const error = frame(
  point(1, 1), point(7, 1),
  point(2, 2), point(6, 2),
  point(3, 3), point(5, 3), point(4, 4),
  point(3, 5), point(5, 5),
  point(2, 6), point(6, 6),
  point(1, 7), point(7, 7),
)

const animated = (frames) => ({ frames, frameDuration: 140 })
const still = (staticFrame) => ({ frames: [staticFrame] })

const ICONS = {
  ready: animated(connectFrames),
  'choosing-device': animated(selectFrames),
  'checking-device': animated(selectFrames),
  'confirm-device': still(confirmedDevice),
  downloading: animated(firmwareFrames),
  'installing-firmware': animated(firmwareFrames),
  reconnecting: animated(connectFrames),
  reconnect: animated(connectFrames),
  'wifi-scanning': animated(wifiPulseFrames),
  wifi: still(wifi),
  configuring: animated(firmwareFrames),
  verifying: animated(wifiPulseFrames),
  complete: animated(completeFrames),
  error: still(error),
}

export function installerIconForPhase(phase) {
  return ICONS[phase] ?? ICONS.ready
}

export function activeInstallerIconCells(iconFrame) {
  return new Set(iconFrame.flatMap((row, y) => [...row].map((value, x) => (
    value === '1' ? (y * GRID_SIZE) + x : -1
  ))).filter((index) => index >= 0))
}
