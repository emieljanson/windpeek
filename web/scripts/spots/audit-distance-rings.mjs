#!/usr/bin/env node
// Read-only review: geometry is exhaustive within the radius; names rank pairs.
import { readFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { distanceMeters } from './lib/duplicate-detection.mjs'
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..')
const catalog = JSON.parse(await readFile(path.join(root, 'src/spots/catalog.generated.json'), 'utf8'))
const maxKm = Number(process.argv[3] ?? 10)
if (!process.argv[2] || !Number.isFinite(maxKm) || maxKm < 1 || maxKm > 25) throw new Error('Usage: audit-distance-rings.mjs <report.json> [radius-km:1..25]')
function normalize(value) {
  return value.normalize('NFKD').replace(/[\u0300-\u036f]/g, '').toLowerCase()
    .replace(/\([^)]*\)/g, '').replace(/\b(sankt|saint)\b/g, 'st')
    .replace(/\b(the|la|le|de|da|do|del|di|el|praia|playa|plage|beach)\b/g, '')
    .replace(/[^\p{L}\p{N}]+/gu, ' ').trim().replace(/\s+/g, ' ')
}
function edits(a, b) {
  let row = Array.from({ length: b.length + 1 }, (_, i) => i)
  for (let i = 0; i < a.length; i++) {
    const next = [i + 1]
    for (let j = 0; j < b.length; j++) next.push(Math.min(next[j] + 1, row[j + 1] + 1, row[j] + Number(a[i] !== b[j])))
    row = next
  }
  return row[b.length]
}
const prepared = catalog.map((s) => ({ ...s, names: [...new Set([s.name, ...(s.aliases ?? [])])].map((name) => ({ name, key: normalize(name) })).filter((n) => n.key.length >= 4) }))
const rings = [...new Set([...([2, 5, 10, 15, 25].filter((km) => km <= maxKm)), maxKm])].map((km) => ({ maxKm: km, allPairs: 0, nameCandidates: 0 }))
const pairs = []
for (let i = 0; i < prepared.length; i++) for (let j = i + 1; j < prepared.length; j++) {
  const a = prepared[i], b = prepared[j]
  if (Math.abs(a.latitude - b.latitude) > maxKm / 110) continue
  const distance = distanceMeters(a, b)
  if (distance > maxKm * 1000) continue
  const ring = rings.find((r) => distance <= r.maxKm * 1000)
  if (ring) ring.allPairs++
  let best
  for (const x of a.names) for (const y of b.names) {
    const xx = x.key.replaceAll(' ', ''), yy = y.key.replaceAll(' ', '')
    let reason
    if (xx === yy) reason = 'normalized-name'
    else if (x.key.split(' ').sort().join(' ') === y.key.split(' ').sort().join(' ')) reason = 'reordered-name'
    else if (Math.min(xx.length, yy.length) >= 7 && Math.abs(xx.length - yy.length) <= 2 && edits(xx, yy) <= (Math.min(xx.length, yy.length) >= 12 ? 2 : 1)) reason = 'spelling-variant'
    if (reason && (!best || reason === 'normalized-name')) best = { reason, leftMatchedName: x.name, rightMatchedName: y.name }
  }
  if (!best) continue
  if (ring) ring.nameCandidates++
  pairs.push({ leftId: a.id, leftName: a.name, rightId: b.id, rightName: b.name, meters: Math.round(distance), countryCodes: [a.countryCode, b.countryCode], ...best })
}
pairs.sort((a, b) => a.meters - b.meters)
await writeFile(process.argv[2], JSON.stringify({ radiusKm: maxKm, locationCount: catalog.length, rings, pairs }, null, 2) + '\n')
console.log(JSON.stringify({ rings, candidates: pairs.length }))
console.log(pairs.map((p) => `${p.meters}m | ${p.leftId} ${p.leftName} / ${p.rightId} ${p.rightName} | ${p.leftMatchedName} / ${p.rightMatchedName}`).join('\n'))
