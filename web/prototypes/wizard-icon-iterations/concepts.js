const SIZE = 9

const pt = (x, y) => [[x, y]]
const h = (y, x1, x2) => Array.from({ length: Math.max(0, x2 - x1 + 1) }, (_, i) => [x1 + i, y])
const v = (x, y1, y2) => Array.from({ length: Math.max(0, y2 - y1 + 1) }, (_, i) => [x, y1 + i])

function rect(x, y, width, height, filled = false) {
  const result = []
  for (let row = y; row < y + height; row += 1) {
    for (let column = x; column < x + width; column += 1) {
      if (filled || row === y || row === y + height - 1 || column === x || column === x + width - 1) result.push([column, row])
    }
  }
  return result
}

const move = (shape, x, y) => shape.map(([column, row]) => [column + x, row + y])
const mirror = (shape) => shape.map(([column, row]) => [SIZE - 1 - column, row])

function frame(...shapes) {
  return new Set(shapes.flat().filter(([x, y]) => x >= 0 && x < SIZE && y >= 0 && y < SIZE).map(([x, y]) => (y * SIZE) + x))
}

const grid = (rows) => frame(...rows.flatMap((row, y) => [...row].flatMap((value, x) => value === '1' ? [[[x, y]]] : [])))

const concept = (name, idea, frames, preview = frames.at(-1)) => ({ name, idea, frames, preview })

function target(radius = 3, length = 2) {
  const left = 4 - radius
  const right = 4 + radius
  const top = 4 - radius
  const bottom = 4 + radius
  return [
    ...h(top, left, left + length - 1), ...v(left, top, top + length - 1),
    ...h(top, right - length + 1, right), ...v(right, top, top + length - 1),
    ...h(bottom, left, left + length - 1), ...v(left, bottom - length + 1, bottom),
    ...h(bottom, right - length + 1, right), ...v(right, bottom - length + 1, bottom),
  ]
}

const centre = pt(4, 4)
const centreBlock = rect(3, 3, 3, 3, true)
const device = rect(2, 3, 5, 4)
const deviceWithBase = [...device, ...h(7, 3, 5)]
const blocks = [...rect(0, 3, 2, 3, true), ...rect(7, 3, 2, 3, true)]
const leftPlug = [...rect(0, 3, 3, 3, true), ...pt(3, 3), ...pt(3, 5)]
const rightPlug = mirror(leftPlug)

const connect = [
  concept('Plug click', 'Two USB-like plug halves click together in the centre.', [
    frame(move(leftPlug, -2, 0), move(rightPlug, 2, 0)), frame(move(leftPlug, -1, 0), move(rightPlug, 1, 0)), frame(leftPlug, rightPlug),
    frame(move(leftPlug, 1, 0), move(rightPlug, -1, 0)), frame(leftPlug, rightPlug),
  ], frame(leftPlug, rightPlug)),
  concept('Soft plug', 'Smaller plug heads meet with a quiet snap.', [0, 1, 2, 1, 2].map((distance) => frame(
    move(rect(0, 3, 2, 3, true), distance, 0), move(rect(7, 3, 2, 3, true), -distance, 0), distance === 2 ? h(4, 3, 5) : [],
  ))),
  concept('Vertical dock', 'A connector drops into a port from above.', [0, 1, 2, 3, 2].map((drop) => frame(
    move(rect(3, -1, 3, 2, true), 0, drop), h(5, 2, 6), v(2, 5, 7), v(6, 5, 7), h(7, 2, 6),
  ))),
  concept('Data bridge', 'Two blocks stay still while a data line grows between them.', [
    frame(blocks), frame(blocks, pt(2, 4), pt(6, 4)), frame(blocks, h(4, 2, 3), h(4, 5, 6)), frame(blocks, h(4, 2, 6)), frame(blocks, h(4, 2, 6)),
  ]),
  concept('Packet bridge', 'A packet travels over the new connection.', [2, 3, 4, 5, 6].map((x) => frame(blocks, h(4, 2, 6), pt(x, 3))), frame(blocks, h(4, 2, 6), pt(4, 3))),
  concept('Double pulse', 'Two packets confirm data in both directions.', [2, 3, 4, 5, 6, 5, 4, 3].map((x) => frame(blocks, h(4, 2, 6), pt(x, 3), pt(8 - x, 5)))),
  concept('Magnetic dock', 'Two simple blocks accelerate and settle together.', [0, 1, 2, 3, 2].map((distance) => frame(
    move(rect(0, 3, 2, 3, true), distance, 0), move(rect(7, 3, 2, 3, true), -distance, 0),
  ))),
  concept('Cable join', 'Two bent cable ends become one continuous cable.', [
    frame(h(2, 0, 2), v(2, 2, 4), h(6, 6, 8), v(6, 4, 6)), frame(h(3, 0, 3), v(3, 3, 4), h(5, 5, 8), v(5, 4, 5)), frame(h(4, 0, 8)), frame(h(4, 0, 8)),
  ]),
  concept('Port insert', 'One plug travels into a clearly drawn port.', [0, 1, 2, 3, 2].map((distance) => frame(
    move(rect(0, 3, 2, 3, true), distance, 0), h(4, 2 + distance, 3 + distance), v(7, 2, 6), h(2, 7, 8), h(6, 7, 8),
  ))),
  concept('Handshake', 'Two hooked data shapes interlock like a handshake.', [
    frame(h(3, 0, 2), v(2, 3, 4), h(5, 6, 8), v(6, 4, 5)), frame(h(3, 1, 3), v(3, 3, 4), h(5, 5, 7), v(5, 4, 5)),
    frame(h(3, 1, 4), v(4, 3, 5), h(5, 4, 7)), frame(h(3, 1, 4), v(4, 3, 5), h(5, 4, 7)),
  ]),
]

const orbit = [[-1, -1], [-1, -1], [-1, 0], [-1, 1], [-1, 1], [0, 1], [1, 1], [1, 1], [1, 0], [1, -1], [1, -1], [0, -1], [-1, -1]]
const reverseOrbit = orbit
const searchSpecs = [
  ['Orbit target', 'One full clockwise search, then centre.', 3, 2, orbit, centre],
  ['Reverse orbit', 'The same search counter-clockwise.', 3, 2, reverseOrbit, centre],
  ['Wide search', 'Large corners search around a tiny centre point.', 4, 2, [[0, -1], [1, 0], [0, 1], [-1, 0], [0, 0]], centre],
  ['Tight search', 'A compact target stays close to the device.', 2, 1, orbit, centre],
  ['Horizontal hunt', 'The selector checks left and right.', 3, 2, [[-1, 0], [1, 0], [-1, 0], [1, 0], [0, 0]], centre],
  ['Vertical hunt', 'The selector checks above and below.', 3, 2, [[0, -1], [0, 1], [0, -1], [0, 1], [0, 0]], centre],
  ['Square patrol', 'The brackets visit four square positions.', 3, 1, [[-1, -1], [1, -1], [1, 1], [-1, 1], [0, 0]], centre],
  ['Device search', 'A larger centre device stays still inside the orbit.', 3, 1, orbit, rect(4, 4, 2, 2, true)],
]
const select = searchSpecs.map(([name, idea, radius, length, route, object]) => concept(name, idea, route.map(([x, y]) => frame(object, move(target(radius, length), x, y)))))
const figmaOrbitRoute = [
  [-1, -1], [-1, 0], [-1, 1], [0, 1],
  [1, 1], [1, 0], [1, -1], [0, -1],
]
select[1] = concept(
  'Reverse orbit',
  'Figma search path around every side of the canvas while the device stays centred.',
  figmaOrbitRoute.map(([x, y]) => frame(rect(3, 3, 3, 3), move(target(3, 2), x, y))),
)
select.push(
  concept('Pulse focus', 'The target breathes from wide to tight.', [4, 3, 2, 3, 4, 3].map((radius) => frame(centre, target(radius, radius === 2 ? 1 : 2)))),
  concept('Corner chase', 'The corners arrive in sequence, then become whole.', [
    frame(centre, target(3, 1).slice(0, 4)), frame(centre, target(3, 1).slice(0, 8)), frame(centre, target(3, 1).slice(0, 12)), frame(centre, target(3, 1)), frame(centre, target(3, 2)),
  ]),
)

const lock = (radius, length, object = centre, verticalWobble = false) => {
  const offsets = verticalWobble ? [[0, -1], [0, 1], [0, -1], [0, 0], [0, 0]] : [[-1, 0], [1, 0], [-1, 0], [0, 0], [0, 0]]
  return offsets.map(([x, y]) => frame(object, move(target(radius, length), x, y)))
}
const confirm = [
  concept('Exact lock', 'The orbit stops with equal space on every side.', lock(3, 2)),
  concept('Quiet lock', 'Smaller corners wobble and settle gently.', lock(3, 1)),
  concept('Tight lock', 'Corners lock close to the centre square.', lock(2, 1)),
  concept('Wide lock', 'The confirmed device gets more breathing room.', lock(4, 2)),
  concept('Vertical settle', 'The target wobbles vertically before stopping.', lock(3, 2, centre, true)),
  concept('Device lock', 'The selection locks around a two-by-two device.', lock(3, 1, rect(4, 4, 2, 2, true))),
  concept('Screen lock', 'The selection locks around a landscape profile.', lock(4, 2, rect(2, 3, 5, 3))),
  concept('Snap inward', 'Wide corners snap into the confirmed position.', [4, 3, 2, 3, 3].map((radius) => frame(centre, target(radius, 1)))),
  concept('Pair confirm', 'The four corners appear in pairs.', [
    frame(centre, target(3, 2).slice(0, 6)), frame(centre, target(3, 2).slice(0, 12)), frame(centre, target(3, 2)), frame(centre, target(3, 2)),
  ]),
  concept('Solid capture', 'The centre resolves into a captured device block.', [
    frame(centre, move(target(3, 1), -1, 0)), frame(centre, move(target(3, 1), 1, 0)), frame(centreBlock, target(3, 1)), frame(centreBlock, target(3, 1)),
  ]),
]

const arrow = [...v(4, 0, 4), ...h(3, 3, 5), ...h(4, 2, 6)]
const rainTrajectories = [
  [1, 0], [1, 4],
  [2, 2], [2, 6],
  [3, 1], [3, 5],
  [4, 3], [4, 7],
  [5, 0], [5, 5],
  [6, 2], [6, 7],
  [7, 1], [7, 5],
]
const dataRainFrames = Array.from({ length: 8 }, (_, step) => frame(
  ...rainTrajectories.map(([x, startingRow]) => pt(x, (startingRow + step) % 8)),
  h(8, 1, 7),
))
const write = [
  concept('Falling arrow', 'A download arrow repeatedly lands on one thin line.', [0, 1, 2, 3, 1].map((drop) => frame(move(arrow, 0, drop - 2), h(8, 2, 6)))),
  concept('Into device', 'A smaller arrow enters the device screen.', [0, 1, 2, 3, 1].map((drop) => frame(move([...v(4, 0, 2), ...h(2, 3, 5)], 0, drop - 1), deviceWithBase))),
  concept('Conveyor', 'Packages ride a transport belt into the device.', [0, 1, 2, 3, 4].map((offset) => frame(h(6, 0, 8), pt((offset * 2) % 9, 5), pt((offset * 2 + 3) % 9, 5), rect(6, 2, 3, 3)))),
  concept('Fill across', 'A container fills from left to right.', [0, 1, 2, 3, 4, 5].map((amount) => frame(rect(1, 2, 7, 5), rect(2, 3, amount, 3, true)))),
  concept('Fill upward', 'A block fills from the bottom upward.', [0, 1, 2, 3, 4, 3, 2].map((amount) => frame(rect(2, 1, 5, 7), rect(3, 7 - amount, 3, amount, true)))),
  concept('Pixel Pong', 'A pixel bounces horizontally between two paddles.', [2, 3, 4, 5, 6, 5, 4, 3].map((x) => frame(v(0, 2, 5), v(8, 3, 6), pt(x, x % 2 ? 3 : 4)))),
  concept('Mini Pong', 'A diagonal Pong rally acts as a playful loader.', [[2, 2], [3, 3], [4, 4], [5, 5], [6, 4], [5, 3], [4, 2], [3, 3]].map(([x, y]) => frame(v(1, 2, 5), v(7, 3, 6), pt(x, y)))),
  concept('Tetris write', 'A block falls and briefly completes a line.', [
    frame(rect(3, 0, 2, 2, true), h(8, 1, 7)), frame(rect(3, 2, 2, 2, true), h(8, 1, 7)), frame(rect(3, 4, 2, 2, true), h(8, 1, 7)), frame(rect(3, 6, 2, 2, true), h(8, 1, 7)), frame(h(8, 0, 8)),
  ]),
  concept('Scan writer', 'A scan head writes one row at a time.', [1, 2, 3, 4, 5, 6, 7].map((y) => frame(rect(1, 1, 7, 7), h(y, 2, 6), pt(y % 2 ? 1 : 7, y)))),
  concept('Data rain', 'Independent packets rain into a receiving line.', dataRainFrames),
]

const reconnect = [
  concept('Line restore', 'The two data blocks rebuild their missing line.', [frame(blocks), frame(blocks, pt(2, 4), pt(6, 4)), frame(blocks, h(4, 2, 3), h(4, 5, 6)), frame(blocks, h(4, 2, 6))]),
  concept('Signal returns', 'A pulse crosses the restored connection and returns.', [2, 3, 4, 5, 6, 5, 4, 3].map((x) => frame(blocks, h(4, 2, 6), pt(x, 3)))),
  concept('Bridge closes', 'Two bridge decks extend until they touch.', [0, 1, 2, 3, 2].map((amount) => frame(v(0, 4, 7), v(8, 4, 7), h(4, 0, amount), h(4, 8 - amount, 8)))),
  concept('Chain repairs', 'Two chain links slide back into one another.', [0, 1, 2, 1].map((distance) => frame(move(rect(0, 2, 4, 5), distance, 0), move(rect(5, 2, 4, 5), -distance, 0)))),
  concept('Plug again', 'A plug backs out, then reconnects.', [2, 1, 0, 1, 2, 3, 2].map((distance) => frame(move(rect(0, 3, 2, 3, true), distance, 0), h(4, 2 + distance, 3 + distance), v(7, 2, 6)))),
  concept('Magnet return', 'Two blocks separate and pull back together.', [3, 2, 1, 0, 1, 2, 3, 2].map((distance) => frame(rect(3 - distance, 3, 2, 3, true), rect(4 + distance, 3, 2, 3, true)))),
  concept('Return loop', 'A packet follows a return route to the device.', [[1, 4], [1, 2], [3, 1], [5, 1], [7, 2], [7, 4], [6, 5], [5, 5]].map(([x, y]) => frame(device, pt(x, y)))),
  concept('Zipper', 'Two separated sides zip together from top to bottom.', [1, 2, 3, 4, 5, 6, 7].map((depth) => frame(...Array.from({ length: 7 }, (_, i) => pt(i < depth ? 4 : (i % 2 ? 6 : 2), i + 1))))),
  concept('Couplers', 'Two mechanical couplers meet and lock.', [0, 1, 2, 1].map((distance) => frame(move([...rect(0, 3, 3, 3, true), ...pt(3, 4)], distance, 0), move(mirror([...rect(0, 3, 3, 3, true), ...pt(3, 4)]), -distance, 0)))),
  concept('Gap repair', 'Loose pixels stitch a broken line together.', [
    frame(h(4, 0, 2), h(4, 6, 8), pt(4, 1), pt(3, 7), pt(5, 0)), frame(h(4, 0, 2), h(4, 6, 8), pt(3, 3), pt(5, 5)), frame(h(4, 0, 3), h(4, 5, 8), pt(4, 2)), frame(h(4, 0, 8)),
  ]),
]

const wifiShapes = [
  ['Classic Wi-Fi', 'Balanced and immediately familiar.', [h(1, 3, 5), pt(2, 2), pt(6, 2), pt(1, 3), pt(7, 3), h(4, 3, 5), pt(2, 5), pt(6, 5), pt(4, 7)]],
  ['Wide signal', 'More open air between wider bands.', [h(1, 1, 7), pt(0, 2), pt(8, 2), h(4, 2, 6), pt(1, 5), pt(7, 5), h(6, 3, 5), pt(4, 8)]],
  ['Compact signal', 'A tight symbol with chunky bands.', [h(2, 2, 6), pt(1, 3), pt(7, 3), h(4, 3, 5), pt(2, 5), pt(6, 5), pt(4, 7)]],
  ['Dotted Wi-Fi', 'Separated pixels imply the radio curves.', [pt(1, 2), pt(3, 1), pt(5, 1), pt(7, 2), pt(2, 4), pt(4, 3), pt(6, 4), pt(3, 6), pt(5, 6), pt(4, 8)]],
  ['Router beacon', 'A small router emits two clear bands.', [rect(2, 6, 5, 2), pt(3, 5), pt(5, 5), h(3, 3, 5), pt(2, 2), pt(6, 2), h(1, 3, 5)]],
  ['Signal bars', 'Wi-Fi as stepped signal strength.', [v(1, 6, 7), v(3, 4, 7), v(5, 2, 7), v(7, 0, 7)]],
  ['Diamond radio', 'Angular diamonds keep the 8-bit character.', [pt(4, 1), pt(2, 3), pt(6, 3), pt(1, 4), pt(7, 4), pt(3, 5), pt(5, 5), pt(4, 7)]],
  ['Device receives', 'A tiny device catches the signal.', [rect(3, 5, 3, 3), pt(4, 4), pt(3, 3), pt(5, 3), pt(2, 2), pt(6, 2), h(1, 3, 5)]],
]
const wifi = wifiShapes.map(([name, idea, shapes]) => concept(name, idea, [frame(...shapes)]))
wifi.splice(4, 0,
  concept('Broadcast pulse', 'The bands radiate outward from the centre.', [frame(pt(4, 7)), frame(pt(4, 7), h(5, 3, 5)), frame(pt(4, 7), h(5, 3, 5), h(3, 2, 6)), frame(pt(4, 7), h(5, 3, 5), h(3, 2, 6), h(1, 2, 6), pt(1, 2), pt(7, 2))]),
  concept('Signal scan', 'One highlight scans across the bands.', [1, 2, 3, 4, 5, 6, 7].map((x) => frame(h(1, 2, 6), h(3, 2, 6), h(5, 3, 5), pt(4, 7), pt(x, 2)))),
)

const apply = [
  concept('Slider snap', 'Three controls snap into final positions.', [[2, 6, 3], [5, 3, 6], [4, 5, 2], [4, 4, 4]].map((p) => frame(h(2, 1, 7), h(4, 1, 7), h(6, 1, 7), pt(p[0], 2), pt(p[1], 4), pt(p[2], 6)))),
  concept('Tile arrange', 'Loose tiles organise into one device.', [frame(rect(0, 0, 2, 2, true), rect(7, 0, 2, 2, true), rect(0, 7, 2, 2, true), rect(7, 7, 2, 2, true)), frame(rect(2, 2, 2, 2, true), rect(5, 2, 2, 2, true), rect(2, 5, 2, 2, true), rect(5, 5, 2, 2, true)), frame(rect(3, 3, 3, 3)), frame(centreBlock)]),
  concept('Config rain', 'Settings fall into a device and settle.', [0, 1, 2, 3, 4].map((drop) => frame(device, pt(3, drop), pt(5, (drop + 2) % 5), h(5, 3, 5)))),
  concept('Stamp setup', 'A configuration stamp lands on the device.', [0, 1, 2, 3, 2].map((drop) => frame(move(rect(3, 0, 3, 2, true), 0, drop), rect(2, 6, 5, 2)))),
  concept('Puzzle fit', 'Four chunky pieces close into a whole.', [0, 1, 2, 1].map((d) => frame(move(rect(0, 0, 2, 2, true), d, d), move(rect(7, 0, 2, 2, true), -d, d), move(rect(0, 7, 2, 2, true), d, -d), move(rect(7, 7, 2, 2, true), -d, -d)))),
  concept('Sync patterns', 'Two patterns converge into one aligned state.', [0, 1, 2, 3, 2].map((d) => frame(v(2 + d, 2, 6), v(6 - d, 2, 6)))),
  concept('Alignment', 'Misaligned lines become one tidy stack.', [frame(h(2, 0, 4), h(4, 3, 8), h(6, 1, 6)), frame(h(2, 1, 5), h(4, 2, 7), h(6, 2, 7)), frame(h(2, 2, 6), h(4, 2, 6), h(6, 2, 6))]),
  concept('Package unpack', 'A package opens and reveals the device.', [frame(rect(2, 2, 5, 5)), frame(h(2, 2, 6), v(2, 2, 6), v(6, 2, 6), pt(3, 3), pt(5, 3)), frame(device, pt(4, 5))]),
  concept('Settings stream', 'Small settings flow into the screen.', [0, 1, 2, 3, 4].map((offset) => frame(device, pt(offset, 1), pt(offset + 2, 2), pt(4, 4)))),
  concept('Lock setup', 'The final setup resolves into a lock.', [frame(pt(2, 2), pt(6, 2), pt(4, 1)), frame(h(2, 3, 5), v(3, 2, 3), v(5, 2, 3)), frame(rect(2, 4, 5, 4), pt(4, 6))]),
]

const pole = v(3, 2, 8)
const complete = [
  concept('Waving flag', 'A finish flag waves in the Windpeek breeze.', [
    frame(pole, h(2, 4, 7), pt(7, 3), h(4, 4, 7)),
    frame(pole, h(2, 4, 6), pt(7, 1), pt(7, 3), h(4, 4, 6)),
    frame(pole, h(2, 4, 5), pt(6, 1), pt(7, 2), h(4, 4, 6)),
    frame(pole, h(2, 4, 5), pt(6, 1), pt(7, 2), pt(7, 3), h(4, 4, 5)),
    frame(pole, h(2, 4, 5), pt(6, 1), pt(7, 2), h(4, 4, 6)),
    frame(pole, h(2, 4, 6), pt(7, 1), pt(7, 3), h(4, 4, 6)),
  ]),
  concept('Pennant', 'A triangular pennant points into the wind.', [frame(pole, h(2, 4, 7), h(3, 4, 6), h(4, 4, 5)), frame(pole, h(2, 4, 6), h(3, 4, 7), h(4, 4, 5))]),
  concept('Windsock', 'A striped windsock becomes the success symbol.', [frame(v(1, 4, 8), h(3, 1, 3), pt(4, 4), pt(5, 5), pt(6, 6)), frame(v(1, 4, 8), h(3, 1, 3), pt(4, 3), pt(5, 4), pt(6, 5)), frame(v(1, 4, 8), h(3, 1, 3), pt(4, 4), pt(5, 5), pt(6, 5))]),
  concept('Weather vane', 'A rooftop weather vane turns and settles.', [frame(v(4, 3, 8), h(3, 1, 7), pt(1, 2), pt(7, 4)), frame(v(4, 3, 8), pt(2, 2), pt(3, 3), pt(5, 3), pt(6, 4)), frame(v(4, 3, 8), h(3, 1, 7), pt(7, 2), pt(1, 4))]),
  concept('Flag unfurls', 'The flag grows outward once, then waves.', [2, 3, 4, 5].map((length) => frame(pole, h(2, 4, 3 + length), h(4, 4, 2 + length)))),
  concept('Twin flags', 'Two small flags face the same breeze.', [frame(v(2, 3, 8), v(6, 3, 8), h(3, 3, 4), h(3, 7, 8)), frame(v(2, 3, 8), v(6, 3, 8), h(3, 3, 5), h(3, 7, 8), pt(8, 4))]),
  concept('Compass vane', 'A wind arrow rotates around a compass point.', [frame(v(4, 1, 7), h(4, 1, 7), pt(4, 0)), frame(pt(2, 2), pt(3, 3), pt(4, 4), pt(5, 5), pt(6, 6), pt(1, 1)), frame(h(4, 1, 7), pt(8, 4))]),
  concept('Breeze ribbons', 'Loose ribbons make completion feel light.', [frame(pt(1, 2), pt(2, 3), pt(3, 2), pt(5, 5), pt(6, 4), pt(7, 5)), frame(pt(1, 3), pt(2, 2), pt(3, 3), pt(5, 4), pt(6, 5), pt(7, 4))]),
  concept('Flag planted', 'A flag drops into place and gives one wave.', [0, 1, 2, 3, 2].map((drop) => frame(move([...pole, ...h(2, 4, 7), ...h(4, 4, 6)], 0, drop - 2)))),
  concept('Wind success', 'A compact breeze resolves with a sparkle.', [frame(h(3, 1, 5), pt(6, 2), pt(7, 3), pt(6, 4)), frame(h(3, 1, 6), h(5, 2, 5), pt(6, 4)), frame(h(3, 1, 6), h(5, 2, 5), pt(6, 4), pt(8, 1), pt(8, 3), pt(7, 2))]),
]

const diagonalCross = (start, end, weight = 1) => {
  const points = []
  for (let y = start; y <= end; y += 1) {
    const offset = y - start
    const left = start + offset
    const right = end - offset
    for (let w = 0; w < weight; w += 1) points.push([left + w, y], [right - w, y])
  }
  return points
}
const error = [
  concept('Thin cross', 'The clearest one-pixel diagonal cross.', [frame(diagonalCross(1, 7))]),
  concept('Bold cross', 'A heavier arcade-style cross.', [frame(diagonalCross(1, 7, 2))]),
  concept('Compact cross', 'A small centred cross with more empty grid.', [frame(diagonalCross(3, 5))]),
  concept('Wide cross', 'A broad cross made from stepped pixels.', [frame(pt(1, 2), pt(7, 2), pt(2, 3), pt(6, 3), pt(3, 4), pt(5, 4), pt(4, 5), pt(3, 6), pt(5, 6))]),
  concept('Boxed cross', 'The cross sits inside a device-like frame.', [frame(rect(1, 1, 7, 7), diagonalCross(3, 5))]),
  concept('Broken cross', 'A cross with a deliberate centre gap.', [frame(pt(1, 1), pt(7, 1), pt(2, 2), pt(6, 2), pt(3, 3), pt(5, 3), pt(3, 5), pt(5, 5), pt(2, 6), pt(6, 6), pt(1, 7), pt(7, 7))]),
  concept('Pixel cross', 'A chunky five-block cross.', [frame(rect(1, 1, 2, 2, true), rect(6, 1, 2, 2, true), rect(3, 3, 3, 3, true), rect(1, 6, 2, 2, true), rect(6, 6, 2, 2, true))]),
  concept('Offset cross', 'Two crossing diagonals feel slightly glitched.', [frame(...Array.from({ length: 7 }, (_, i) => [[i + 1, i + 1], [8 - i, i + 1]]))]),
  concept('Dotted cross', 'Sparse diagonal dots reduce the weight.', [frame(pt(1, 1), pt(7, 1), pt(3, 3), pt(5, 3), pt(4, 4), pt(3, 5), pt(5, 5), pt(1, 7), pt(7, 7))]),
  concept('Cross burst', 'A compact cross with four error fragments.', [frame(pt(3, 3), pt(5, 3), pt(4, 4), pt(3, 5), pt(5, 5), pt(4, 0), pt(8, 4), pt(4, 8), pt(0, 4))]),
]

export const stepConcepts = { connect, select, confirm, write, reconnect, wifi, apply, complete, error }
