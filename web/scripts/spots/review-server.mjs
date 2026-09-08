import { readFile, rename, writeFile } from 'node:fs/promises'
import path from 'node:path'

const MAX_BODY_BYTES = 32_768

async function readJson(filePath, fallback) {
  try {
    return JSON.parse(await readFile(filePath, 'utf8'))
  } catch (error) {
    if (error.code === 'ENOENT') return fallback
    throw error
  }
}

async function atomicWrite(filePath, value) {
  const temporaryPath = `${filePath}.${process.pid}.tmp`
  await writeFile(temporaryPath, `${JSON.stringify(value, null, 2)}\n`, { flag: 'wx' })
  await rename(temporaryPath, filePath)
}

function json(response, status, value) {
  response.statusCode = status
  response.setHeader('Content-Type', 'application/json; charset=utf-8')
  response.setHeader('Cache-Control', 'no-store')
  response.end(JSON.stringify(value))
}

async function requestBody(request) {
  let body = ''
  for await (const chunk of request) {
    body += chunk
    if (Buffer.byteLength(body) > MAX_BODY_BYTES) throw new Error('Request is too large.')
  }
  return JSON.parse(body)
}

function validDecision(decision) {
  const base = decision && typeof decision.candidateId === 'string' && decision.candidateId &&
    typeof decision.evidenceFingerprint === 'string' && decision.evidenceFingerprint &&
    ['approve', 'reject'].includes(decision.action)
  if (!base) return false
  if (decision.action === 'reject') return typeof decision.reason === 'string' && Boolean(decision.reason.trim())
  const latitude = Number(decision.latitude)
  const longitude = Number(decision.longitude)
  return typeof decision.windpeekId === 'string' && Boolean(decision.windpeekId) &&
    typeof decision.name === 'string' && Boolean(decision.name.trim()) &&
    Number.isFinite(latitude) && latitude >= -90 && latitude <= 90 &&
    Number.isFinite(longitude) && longitude >= -180 && longitude <= 180
}

export function spotReviewPlugin({ dataRoot }) {
  const candidatesPath = path.join(dataRoot, 'candidates.json')
  const resultsPath = path.join(dataRoot, 'validation-results.json')
  const decisionsPath = path.join(dataRoot, 'review-decisions.json')
  return {
    name: 'windpeek-spot-review',
    configureServer(server) {
      server.middlewares.use('/api/spot-review', async (request, response) => {
        try {
          if (request.method === 'GET' && request.url === '/') {
            const [candidates, validation, decisions] = await Promise.all([
              readJson(candidatesPath, { candidates: [] }),
              readJson(resultsPath, { results: [], duplicateGroups: [] }),
              readJson(decisionsPath, { version: 1, decisions: [] }),
            ])
            return json(response, 200, {
              candidates: candidates.candidates,
              results: validation.results,
              duplicateGroups: validation.duplicateGroups,
              decisions: decisions.decisions,
            })
          }
          if (request.method === 'POST' && request.url === '/decision') {
            const decision = await requestBody(request)
            if (!validDecision(decision)) return json(response, 400, { error: 'Invalid review decision.' })
            const envelope = await readJson(decisionsPath, { version: 1, decisions: [] })
            const decisions = (envelope.decisions ?? []).filter((item) => item.candidateId !== decision.candidateId)
            decisions.push(decision)
            decisions.sort((left, right) => left.candidateId.localeCompare(right.candidateId))
            await atomicWrite(decisionsPath, { version: 1, decisions })
            return json(response, 200, { ok: true })
          }
          return json(response, 404, { error: 'Not found.' })
        } catch (error) {
          return json(response, 400, { error: error.message || 'Review data could not be updated.' })
        }
      })
    },
  }
}
