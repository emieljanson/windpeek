export async function readBody(request, maxBytes) {
  if (Number(request.headers.get('content-length')) > maxBytes) return null
  const reader = request.body?.getReader()
  if (!reader) return null
  const chunks = []
  let size = 0
  try {
    while (true) {
      const { value, done } = await reader.read()
      if (done) break
      size += value.byteLength
      if (size > maxBytes) { await reader.cancel(); return null }
      chunks.push(value)
    }
  } finally { reader.releaseLock() }
  const body = new Uint8Array(size)
  let offset = 0
  for (const chunk of chunks) { body.set(chunk, offset); offset += chunk.byteLength }
  return body
}

export async function readJson(request, maxBytes) {
  const body = await readBody(request, maxBytes)
  if (!body) return null
  return JSON.parse(new TextDecoder().decode(body))
}
