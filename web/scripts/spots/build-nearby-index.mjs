#!/usr/bin/env node

import { readFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { buildNearbyIndex } from './lib/nearby-index.mjs'

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..')
const dataRoot = path.join(webRoot, 'data/spots')
const outputPath = path.join(webRoot, 'src/spots/nearby.generated.json')
const checkOnly = process.argv.includes('--check')

const [candidateData, catalog, popularData] = await Promise.all([
  readJson(path.join(dataRoot, 'candidates.json')),
  readJson(path.join(webRoot, 'src/spots/catalog.generated.json')),
  readJson(path.join(dataRoot, 'popular-spots.json')),
])

const index = buildNearbyIndex({
  candidates: candidateData.candidates ?? [],
  catalog,
  popularSpots: popularData.spots ?? [],
  baselineSpotIds: ['edam', 'castricum-aan-zee'],
})
const output = `${JSON.stringify(index, null, 2)}\n`

if (checkOnly) {
  if (await readFile(outputPath, 'utf8') !== output) {
    throw new Error('src/spots/nearby.generated.json is stale.')
  }
} else {
  await writeFile(outputPath, output)
}

console.log(`Nearby index contains ${index.length} surf spots.`)

async function readJson(filePath) {
  return JSON.parse(await readFile(filePath, 'utf8'))
}
