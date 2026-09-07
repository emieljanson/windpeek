#!/usr/bin/env node

import { readFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { buildRuntimeCatalog } from './lib/catalog-builder.mjs'

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..')
const dataRoot = path.join(webRoot, 'data/spots')
const outputPath = path.join(webRoot, 'src/spots/catalog.generated.json')
const checkOnly = process.argv.includes('--check')
const existing = [
  { id: 'edam', name: 'Edam', displayName: 'EDAM', latitude: 52.5126, longitude: 5.0486, timezone: 'Europe/Amsterdam', countryCode: 'nl' },
  { id: 'brouwersdam', name: 'Brouwersdam', displayName: 'BROUWERSDAM', latitude: 51.7506, longitude: 3.8577, timezone: 'Europe/Amsterdam', countryCode: 'nl' },
  { id: 'castricum-aan-zee', name: 'Castricum aan Zee', displayName: 'CASTRICUM AAN ZEE', latitude: 52.555, longitude: 4.609, timezone: 'Europe/Amsterdam', countryCode: 'nl' },
  { id: 'third-avenue', name: 'Third Avenue', displayName: 'THIRD AVENUE', latitude: 37.57457, longitude: -122.283926, timezone: 'America/Los_Angeles', countryCode: 'us' },
]

async function json(filePath, fallback) {
  try {
    return JSON.parse(await readFile(filePath, 'utf8'))
  } catch (error) {
    if (error.code === 'ENOENT' && fallback !== undefined) return fallback
    throw error
  }
}

const [candidateData, validationData, decisionData] = await Promise.all([
  json(path.join(dataRoot, 'candidates.json')),
  json(path.join(dataRoot, 'validation-results.json')),
  json(path.join(dataRoot, 'review-decisions.json'), { decisions: [] }),
])
const catalog = buildRuntimeCatalog({
  existing,
  candidates: candidateData.candidates ?? [],
  validationResults: validationData.results ?? [],
  decisions: decisionData.decisions ?? [],
})
const output = `${JSON.stringify(catalog, null, 2)}\n`
if (checkOnly) {
  if (await readFile(outputPath, 'utf8') !== output) throw new Error('src/spots/catalog.generated.json is stale.')
} else {
  await writeFile(outputPath, output)
}
console.log(`Catalog contains ${catalog.length} spots.`)
