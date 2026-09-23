import { execFileSync } from 'node:child_process'
import { chmod, mkdir, readFile, rename, rm, stat } from 'node:fs/promises'
import { dirname, join, relative } from 'node:path'
import { fileURLToPath } from 'node:url'

const EMSCRIPTEN_IMAGE = 'emscripten/emsdk:4.0.10'
const webRoot = dirname(dirname(fileURLToPath(import.meta.url)))
const repositoryRoot = dirname(webRoot)
const outputDirectory = join(webRoot, 'public', 'renderer')
const temporaryOutput = join(outputDirectory, 'wind-renderer.tmp.wasm')
const finalOutput = join(outputDirectory, 'wind-renderer.wasm')
const checkOnly = process.argv.includes('--check')

const sources = [
  'web/wasm/wind_renderer_bridge.c',
  'firmware/main/wind_renderer.c',
  'firmware/main/wind_font.c',
  'firmware/main/fonts/berkeley_mono_bold_15.c',
  'firmware/main/fonts/berkeley_mono_bold_condensed_12.c',
  'firmware/main/fonts/berkeley_mono_bold_condensed_15.c',
  'firmware/main/fonts/inter_43.c',
]

async function main() {
  await mkdir(outputDirectory, { recursive: true })
  await rm(temporaryOutput, { force: true })

  const outputInContainer = relative(repositoryRoot, temporaryOutput)
  const args = [
    'run', '--rm', '--platform', 'linux/amd64',
    '--volume', `${repositoryRoot}:/src`,
    '--workdir', '/src',
    EMSCRIPTEN_IMAGE,
    'emcc',
    ...sources,
    '-Ifirmware/main',
    '-Ishared/renderer-fixtures',
    '-std=c11',
    '-Wall',
    '-Wextra',
    '-Werror',
    // O2 is deliberate: emsdk 4.0.10's O3 compiler pass crashes under the
    // amd64 emulation used on Apple Silicon, while O2 remains deterministic.
    '-O2',
    '--no-entry',
    '-sSTANDALONE_WASM=1',
    '-sALLOW_MEMORY_GROWTH=0',
    '-sINITIAL_MEMORY=33554432',
    '-sSTACK_SIZE=262144',
    '-Wl,--strip-all',
    '-o', outputInContainer,
  ]

  try {
    execFileSync('docker', args, { cwd: repositoryRoot, stdio: 'inherit' })
    const result = await stat(temporaryOutput)
    if (result.size === 0) throw new Error('Emscripten produced an empty module')
    if (checkOnly) {
      const [expected, actual] = await Promise.all([readFile(finalOutput), readFile(temporaryOutput)])
      if (!expected.equals(actual)) throw new Error('The checked-in WebAssembly renderer is stale; run npm run renderer:build')
      await rm(temporaryOutput, { force: true })
      console.log(`Verified ${relative(repositoryRoot, finalOutput)} with ${EMSCRIPTEN_IMAGE} (${result.size} bytes)`)
    } else {
      await rename(temporaryOutput, finalOutput)
      await chmod(finalOutput, 0o644)
      console.log(`Built ${relative(repositoryRoot, finalOutput)} with ${EMSCRIPTEN_IMAGE} (${result.size} bytes)`)
    }
  } catch (error) {
    await rm(temporaryOutput, { force: true })
    throw error
  }
}

await main()
