#!/usr/bin/env node

import { readFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { importOsmElements } from './lib/osm-source.mjs'
import { importVarunRecords } from './lib/varun-source.mjs'
import { importSurfForecastRecords } from './lib/surf-forecast-source.mjs'

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..')
const dataRoot = path.join(webRoot, 'data/spots')
const manifestPath = path.join(dataRoot, 'source-manifest.json')
const candidatesPath = path.join(dataRoot, 'candidates.json')
const resolutionsPath = path.join(dataRoot, 'link-resolutions.json')
const checkOnly = process.argv.includes('--check')

async function readJson(filePath, fallback) {
  try {
    return JSON.parse(await readFile(filePath, 'utf8'))
  } catch (error) {
    if (error.code === 'ENOENT' && fallback !== undefined) return fallback
    throw error
  }
}

function sourceEligibility(source) {
  const rights = source?.rights ?? {}
  return source?.releaseEligible === true && rights.redistribution === true &&
    typeof rights.license === 'string' && rights.license &&
    typeof rights.attribution === 'string' && rights.attribution
}

function stableJson(value) {
  return `${JSON.stringify(value, null, 2)}\n`
}

async function writeOrCheck(filePath, value) {
  const next = stableJson(value)
  if (!checkOnly) {
    await writeFile(filePath, next)
    return
  }
  const current = await readFile(filePath, 'utf8')
  if (current !== next) throw new Error(`${path.relative(webRoot, filePath)} is stale; run npm run spots:import`)
}

const manifest = await readJson(manifestPath)
const resolutions = await readJson(resolutionsPath, {})
const candidates = []
const exclusions = []
const failures = []
const sources = {}

for (const source of manifest.sources ?? []) {
  const inputPath = path.resolve(dataRoot, source.input)
  const input = await readJson(inputPath)
  const releaseEligible = Boolean(sourceEligibility(source))
  let result
  if (source.adapter === 'varun') {
    result = await importVarunRecords(input, { resolutions, releaseEligible })
  } else if (source.adapter === 'osm') {
    result = importOsmElements(input.elements, { releaseEligible })
  } else if (source.adapter === 'surf-forecast') {
    result = importSurfForecastRecords(input.records, { releaseEligible, excludedRecords: source.excludedRecords })
  } else {
    throw new Error(`Unsupported spot source adapter: ${source.adapter}`)
  }
  candidates.push(...result.candidates)
  exclusions.push(...(result.exclusions ?? []).map((item) => ({ source: source.id, ...item })))
  failures.push(...(result.failures ?? []).map((item) => ({ source: source.id, ...item })))
  sources[source.id] = {
    candidates: result.candidates.length,
    exclusions: result.exclusions?.length ?? 0,
    failures: result.failures?.length ?? 0,
    releaseEligible,
  }
}

candidates.sort((left, right) => left.id.localeCompare(right.id))
exclusions.sort((left, right) => `${left.source}:${left.sourceId}`.localeCompare(`${right.source}:${right.sourceId}`))
failures.sort((left, right) => `${left.source}:${left.sourceId}`.localeCompare(`${right.source}:${right.sourceId}`))

await writeOrCheck(candidatesPath, {
  version: 1,
  sources,
  candidates,
  exclusions,
  failures,
})
await writeOrCheck(resolutionsPath, resolutions)

console.log(JSON.stringify({
  candidates: candidates.length,
  exclusions: exclusions.length,
  failures: failures.length,
  sources,
}, null, 2))
