import { spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { writeFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { resolveLocalBundle, selectLocalFirmwareBuild } from './local-installer-build.mjs'

const webDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const repositoryDir = path.resolve(webDir, '..')
const partitionsPath = path.join(repositoryDir, 'firmware', 'partitions.csv')
const firmwareOutputDir = path.join(webDir, 'public', 'firmware')
const generatorPath = path.join(repositoryDir, 'firmware', 'scripts', 'generate_installer_manifest.py')

let prepared = 0
for (const [boardId, label, directory] of [
  ['seeedstudio_reterminal_e1002', 'E1001/E1002', firmwareOutputDir],
  ['seeedstudio_reterminal_e1003', 'E1003', path.join(firmwareOutputDir, 'e1003')],
]) {
  let selected
  try {
    selected = selectLocalFirmwareBuild(repositoryDir, boardId)
  } catch (error) {
    if (/^No complete local/.test(error.message)) continue
    console.error(`[installer] ${error.message}`)
    process.exit(1)
  }

  let localBundle
  try {
    localBundle = resolveLocalBundle(selected, directory)
  } catch (error) {
    console.error(`[installer] ${error.message}`)
    process.exit(1)
  }
  const { version } = localBundle
  if (localBundle.reuseExisting) {
    const pointer = {
      version,
      manifest: `${version}/installer-manifest.json`,
      sha256: createHash('sha256').update(localBundle.manifestBytes).digest('hex'),
    }
    writeFileSync(path.join(directory, 'latest.json'), `${JSON.stringify(pointer, null, 2)}\n`)
  } else {
    const result = spawnSync(process.env.PYTHON || 'python3', [
      generatorPath,
      '--build-dir', selected.buildDir,
      '--partitions', partitionsPath,
      '--output', directory,
      '--version', version,
      '--board-id', boardId,
    ], { stdio: 'inherit' })
    if (result.error || result.status !== 0) {
      console.error(`[installer] Could not prepare the local ${label} firmware bundle.`)
      process.exit(result.status || 1)
    }
  }
  prepared += 1
  console.log(`[installer] Local ${label} firmware ${version} is ready.`)
}
if (!prepared) console.warn('[installer] No local firmware build found. Build a Windpeek board before testing installation on a device.')
