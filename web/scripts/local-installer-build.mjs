import { createHash } from 'node:crypto'
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs'
import path from 'node:path'

const SOURCE_ENTRIES = ['CMakeLists.txt', 'sdkconfig.defaults', 'boards', 'main', 'components']
const PART_KINDS = {
  'bootloader/bootloader.bin': 'bootloader',
  'partition_table/partition-table.bin': 'partition-table',
  'ota_data_initial.bin': 'boot-selection',
  'windpeek.bin': 'application',
}

function partKind(sourceName) {
  if (PART_KINDS[sourceName]) return PART_KINDS[sourceName]
  const basename = path.basename(sourceName)
  if (basename === 'bootloader.bin') return 'bootloader'
  if (basename === 'partition-table.bin') return 'partition-table'
  if (basename === 'ota_data_initial.bin') return 'boot-selection'
  if (basename === 'windpeek.bin') return 'application'
  return null
}

function newestMtime(entry) {
  if (!existsSync(entry)) return 0
  const details = statSync(entry)
  if (!details.isDirectory()) return details.mtimeMs
  return readdirSync(entry, { withFileTypes: true }).reduce((newest, child) => {
    if (child.name.startsWith('build')) return newest
    return Math.max(newest, newestMtime(path.join(entry, child.name)))
  }, 0)
}

function readCandidate(buildDir) {
  const flasherArgsPath = path.join(buildDir, 'flasher_args.json')
  const projectDescriptionPath = path.join(buildDir, 'project_description.json')
  const sdkconfigPath = path.join(buildDir, 'config', 'sdkconfig.json')
  if (!existsSync(flasherArgsPath) || !existsSync(projectDescriptionPath) ||
      !existsSync(sdkconfigPath)) return null

  const flasherArgs = JSON.parse(readFileSync(flasherArgsPath, 'utf8'))
  const projectDescription = JSON.parse(readFileSync(projectDescriptionPath, 'utf8'))
  const sdkconfig = JSON.parse(readFileSync(sdkconfigPath, 'utf8'))
  if (sdkconfig.BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X !== true) return null
  const flashFiles = Object.entries(flasherArgs.flash_files ?? {})
    .sort(([left], [right]) => Number.parseInt(left, 16) - Number.parseInt(right, 16))
  const files = flashFiles.map(([, relativeFile]) => path.join(buildDir, relativeFile))
  if (!flashFiles.length || files.some((file) => !existsSync(file))) return null
  if (typeof projectDescription.project_version !== 'string' || !projectDescription.project_version.trim()) return null
  const appFile = projectDescription.app_bin
    ? path.join(buildDir, projectDescription.app_bin)
    : files.find((file) => path.basename(file) === 'windpeek.bin')
  if (!appFile || !existsSync(appFile)) return null

  return {
    buildDir,
    flasherArgsPath,
    flashFiles,
    files,
    version: projectDescription.project_version.trim(),
    // Incremental ESP-IDF builds correctly leave unchanged bootloader and OTA
    // helper binaries untouched. The application binary is the artifact that
    // incorporates firmware/main, board settings and project configuration,
    // so use its timestamp for source-staleness checks.
    builtAt: statSync(appFile).mtimeMs,
  }
}

export function selectLocalFirmwareBuild(repositoryDir) {
  const firmwareDir = path.join(repositoryDir, 'firmware')
  const candidates = ['build-local', 'build']
    .map((directory) => readCandidate(path.join(firmwareDir, directory)))
    .filter(Boolean)
    .sort((left, right) => right.builtAt - left.builtAt)

  if (!candidates.length) {
    throw new Error('No complete local E1001/E1002 firmware build was found.')
  }

  const selected = candidates[0]
  const newestSource = Math.max(...SOURCE_ENTRIES.map((entry) => newestMtime(path.join(firmwareDir, entry))))
  if (newestSource > selected.builtAt) {
    throw new Error('The local firmware build is older than the firmware source. Rebuild the universal E1001/E1002 firmware before starting the installer.')
  }
  return selected
}

export function resolveLocalBundle(selected, outputDir) {
  const bundleDir = path.join(outputDir, selected.version)
  const manifestPath = path.join(bundleDir, 'installer-manifest.json')
  if (!existsSync(manifestPath)) return { version: selected.version, reuseExisting: false }

  let manifestBytes
  let matches = false
  try {
    manifestBytes = readFileSync(manifestPath)
    const manifest = JSON.parse(manifestBytes.toString('utf8'))
    if (manifest.version !== selected.version || !Array.isArray(manifest.parts) || manifest.parts.length !== 4) {
      throw new Error('identity mismatch')
    }
    for (const part of manifest.parts) {
      if (path.basename(part.file) !== part.file) throw new Error('unsafe part path')
      const publishedPath = path.join(bundleDir, part.file)
      if (!existsSync(publishedPath)) throw new Error('missing part')
      const publishedHash = createHash('sha256').update(readFileSync(publishedPath)).digest('hex')
      if (publishedHash !== part.sha256) throw new Error('part checksum mismatch')
    }
    const publishedHashes = new Map((manifest.parts ?? []).map((part) => [part.kind, part.sha256]))
    matches = selected.flashFiles.every(([, sourceName], index) => {
      const kind = partKind(sourceName)
      if (!kind || !publishedHashes.has(kind)) return false
      const sourceHash = createHash('sha256').update(readFileSync(selected.files[index])).digest('hex')
      return publishedHashes.get(kind) === sourceHash
    })
  } catch (error) {
    throw new Error(`The existing local firmware bundle for ${selected.version} is incomplete or corrupt.`, { cause: error })
  }
  if (!matches) {
    throw new Error(`Firmware version ${selected.version} already belongs to a different build. Rebuild with a new firmware version so the installer can never serve stale device UI.`)
  }
  return {
    version: selected.version,
    reuseExisting: true,
    manifestBytes,
  }
}
