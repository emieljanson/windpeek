import { createHash } from 'node:crypto'
import { mkdtempSync, mkdirSync, readFileSync, rmSync, utimesSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { afterEach, describe, expect, it } from 'vitest'
import { resolveLocalBundle, selectLocalFirmwareBuild } from '../../scripts/local-installer-build.mjs'

const temporaryDirectories = []

function fixture() {
  const root = mkdtempSync(path.join(tmpdir(), 'windpeek-local-build-'))
  temporaryDirectories.push(root)
  const firmware = path.join(root, 'firmware')
  mkdirSync(path.join(firmware, 'main'), { recursive: true })
  writeFileSync(path.join(firmware, 'main', 'windpeek_main.c'), 'void app_main(void) {}')
  return { root, firmware }
}

function build(firmware, directory, version, timestamp, {
  auxiliaryTimestamp = timestamp,
  universal = true,
} = {}) {
  const buildDirectory = path.join(firmware, directory)
  mkdirSync(path.join(buildDirectory, 'bootloader'), { recursive: true })
  mkdirSync(path.join(buildDirectory, 'partition_table'), { recursive: true })
  mkdirSync(path.join(buildDirectory, 'config'), { recursive: true })
  writeFileSync(path.join(buildDirectory, 'windpeek.bin'), version)
  writeFileSync(path.join(buildDirectory, 'bootloader', 'bootloader.bin'), 'unchanged bootloader')
  writeFileSync(path.join(buildDirectory, 'partition_table', 'partition-table.bin'), 'partition table')
  writeFileSync(path.join(buildDirectory, 'ota_data_initial.bin'), 'boot selection')
  writeFileSync(path.join(buildDirectory, 'flasher_args.json'), JSON.stringify({
    flash_files: {
      '0x0': 'bootloader/bootloader.bin',
      '0x8000': 'partition_table/partition-table.bin',
      '0xf000': 'ota_data_initial.bin',
      '0x20000': 'windpeek.bin',
    },
  }))
  writeFileSync(path.join(buildDirectory, 'project_description.json'), JSON.stringify({
    project_version: version,
    app_bin: 'windpeek.bin',
  }))
  writeFileSync(path.join(buildDirectory, 'config', 'sdkconfig.json'), JSON.stringify({
    BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E100X: universal,
  }))
  for (const file of [
    'windpeek.bin', 'partition_table/partition-table.bin', 'ota_data_initial.bin',
    'flasher_args.json', 'project_description.json', 'config/sdkconfig.json',
  ]) {
    utimesSync(path.join(buildDirectory, file), timestamp, timestamp)
  }
  utimesSync(path.join(buildDirectory, 'bootloader', 'bootloader.bin'), auxiliaryTimestamp, auxiliaryTimestamp)
  return buildDirectory
}

afterEach(() => {
  for (const directory of temporaryDirectories.splice(0)) rmSync(directory, { recursive: true })
})

describe('local installer firmware build', () => {
  it('selects the newest complete build and uses its embedded firmware version', () => {
    const { root, firmware } = fixture()
    const sourceTime = new Date('2026-08-30T10:00:00Z')
    utimesSync(path.join(firmware, 'main', 'windpeek_main.c'), sourceTime, sourceTime)
    build(firmware, 'build', 'old-version', new Date('2026-08-30T11:00:00Z'))
    const newest = build(firmware, 'build-local', 'dev-current', new Date('2026-08-30T12:00:00Z'))

    expect(selectLocalFirmwareBuild(root)).toMatchObject({
      buildDir: newest,
      version: 'dev-current',
    })
  })

  it('rejects a build older than the firmware sources', () => {
    const { root, firmware } = fixture()
    build(firmware, 'build', 'stale-version', new Date('2026-08-30T10:00:00Z'))
    const sourceTime = new Date('2026-08-30T11:00:00Z')
    utimesSync(path.join(firmware, 'main', 'windpeek_main.c'), sourceTime, sourceTime)

    expect(() => selectLocalFirmwareBuild(root)).toThrow(/older than the firmware source/i)
  })

  it('accepts an incremental build when only an unchanged helper binary is older', () => {
    const { root, firmware } = fixture()
    const sourceTime = new Date('2026-08-30T11:00:00Z')
    utimesSync(path.join(firmware, 'main', 'windpeek_main.c'), sourceTime, sourceTime)
    const current = build(firmware, 'build-local', 'dev-current', new Date('2026-08-30T12:00:00Z'), {
      auxiliaryTimestamp: new Date('2026-08-29T08:00:00Z'),
    })

    expect(selectLocalFirmwareBuild(root)).toMatchObject({
      buildDir: current,
      version: 'dev-current',
    })
  })

  it('ignores a model-specific build that cannot install E1001', () => {
    const { root, firmware } = fixture()
    const sourceTime = new Date('2026-08-30T10:00:00Z')
    utimesSync(path.join(firmware, 'main', 'windpeek_main.c'), sourceTime, sourceTime)
    const universal = build(
      firmware,
      'build-local',
      'universal-dev',
      new Date('2026-08-30T11:00:00Z'),
    )
    build(firmware, 'build', 'e1003-only', new Date('2026-08-30T12:00:00Z'), {
      universal: false,
    })

    expect(selectLocalFirmwareBuild(root)).toMatchObject({ buildDir: universal })
  })

  it('refuses a different build with the same embedded firmware version', () => {
    const { root, firmware } = fixture()
    const timestamp = new Date('2026-08-30T12:00:00Z')
    utimesSync(path.join(firmware, 'main', 'windpeek_main.c'), timestamp, timestamp)
    const buildDirectory = build(firmware, 'build-local', 'dev-current', timestamp)
    const selected = selectLocalFirmwareBuild(root)
    const output = path.join(root, 'web', 'public', 'firmware')
    const bundle = path.join(output, 'dev-current')
    mkdirSync(bundle, { recursive: true })
    const publishedBootloader = path.join(bundle, 'bootloader-dev-current.bin')
    const publishedApplication = path.join(bundle, 'application-dev-current.bin')
    writeFileSync(publishedBootloader, 'different bootloader')
    writeFileSync(publishedApplication, 'different application')
    writeFileSync(path.join(bundle, 'partition-table-dev-current.bin'), 'partition')
    writeFileSync(path.join(bundle, 'boot-selection-dev-current.bin'), 'selection')
    const part = (kind, file) => ({
      kind,
      file: path.basename(file),
      sha256: createHash('sha256').update(readFileSync(file)).digest('hex'),
    })
    writeFileSync(path.join(bundle, 'installer-manifest.json'), JSON.stringify({
      version: 'dev-current',
      parts: [
        part('bootloader', publishedBootloader),
        part('application', publishedApplication),
        part('partition-table', path.join(bundle, 'partition-table-dev-current.bin')),
        part('boot-selection', path.join(bundle, 'boot-selection-dev-current.bin')),
      ],
    }))

    expect(() => resolveLocalBundle(selected, output)).toThrow(/new firmware version/i)
    expect(buildDirectory).toBe(selected.buildDir)
  })

  it('reuses an immutable version when every published part matches the selected build', () => {
    const { root, firmware } = fixture()
    const timestamp = new Date('2026-08-30T12:00:00Z')
    utimesSync(path.join(firmware, 'main', 'windpeek_main.c'), timestamp, timestamp)
    build(firmware, 'build-local', 'dev-current', timestamp)
    const selected = selectLocalFirmwareBuild(root)
    const output = path.join(root, 'web', 'public', 'firmware')
    const bundle = path.join(output, 'dev-current')
    mkdirSync(bundle, { recursive: true })
    const kinds = {
      'bootloader/bootloader.bin': 'bootloader',
      'partition_table/partition-table.bin': 'partition-table',
      'ota_data_initial.bin': 'boot-selection',
      'windpeek.bin': 'application',
    }
    const parts = selected.flashFiles.map(([, sourceName], index) => {
      const kind = kinds[sourceName]
      const file = `${kind}-dev-current.bin`
      writeFileSync(path.join(bundle, file), readFileSync(selected.files[index]))
      return {
        kind,
        file,
        sha256: createHash('sha256').update(readFileSync(selected.files[index])).digest('hex'),
      }
    })
    writeFileSync(path.join(bundle, 'installer-manifest.json'), JSON.stringify({ version: 'dev-current', parts }))

    expect(resolveLocalBundle(selected, output)).toMatchObject({ version: 'dev-current', reuseExisting: true })
  })
})
