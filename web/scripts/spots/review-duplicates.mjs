#!/usr/bin/env node
// Reports automatic suppressions and remaining review candidates.
import { readFile, writeFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { detectDuplicates, distanceMeters, selectDuplicateSuppressions } from './lib/duplicate-detection.mjs'
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../data/spots')
const { candidates } = JSON.parse(await readFile(path.join(root, 'candidates.json'), 'utf8'))
const groups = detectDuplicates(candidates)
const suppressed = selectDuplicateSuppressions(candidates, groups)
const retained = candidates.filter((c) => !suppressed.has(c.id))
const nameKey = (name) => name.normalize('NFKD').replace(/[\u0300-\u036f]/g, '').toLowerCase()
  .replace(/\b(praia|playa|plage|beach|de|da|do|del|la|le|the)\b/g, '').replace(/[^\p{L}\p{N}]/gu, '')
const variants = []
for (let i = 0; i < retained.length; i += 1) {
  const a = retained[i], key = nameKey(a.name)
  if (key.length < 4) continue
  for (let j = i + 1; j < retained.length; j += 1) {
    const b = retained[j]
    if (Math.abs(a.latitude - b.latitude) > 0.046 || key !== nameKey(b.name)) continue
    const distance = distanceMeters(a, b)
    if (distance > 5000) continue
    variants.push({ leftId: a.id, leftName: a.name, rightId: b.id, rightName: b.name, distanceMeters: Math.round(distance) })
  }
}
const byId = new Map(candidates.map((c) => [c.id, c]))
const proximityOnly = groups.filter((g) => g.reasons.length === 1 && g.reasons[0] === 'within-75m' &&
  !suppressed.has(g.leftId) && !suppressed.has(g.rightId))
  .map((g) => ({ ...g, leftName: byId.get(g.leftId).name, rightName: byId.get(g.rightId).name }))
const report = { automaticSuppressions: [...suppressed].sort(), nameVariants: variants, proximityOnly }
const output = process.argv[2]
if (!output) throw new Error('Usage: node scripts/spots/review-duplicates.mjs <report.json>')
await writeFile(output, `${JSON.stringify(report, null, 2)}\n`)
console.log({ automaticSuppressions: suppressed.size, nameVariants: variants.length, proximityOnly: proximityOnly.length })
