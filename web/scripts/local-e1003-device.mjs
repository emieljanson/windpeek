import { createHash } from 'node:crypto'
import { readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { createServer } from 'vite'

const webDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const repositoryDir = path.resolve(webDir, '..')
const build = spawnSync('python3', [
  path.join(repositoryDir, 'firmware', 'scripts', 'build_local_e1003.py'),
], { cwd: repositoryDir, stdio: 'inherit' })
if (build.error) throw build.error
if (build.status !== 0) process.exit(build.status ?? 1)

const buildDir = path.join(tmpdir(), 'windpeek-e1003-local-build')
const config = JSON.parse(readFileSync(path.join(buildDir, 'config', 'sdkconfig.json'), 'utf8'))
const flashFiles = JSON.parse(readFileSync(path.join(buildDir, 'flasher_args.json'), 'utf8')).flash_files
if (config.BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1003 !== true ||
    flashFiles['0x20000'] !== 'windpeek.bin') {
  throw new Error('The build is not an E1003 app at the expected flash address.')
}
const firmware = readFileSync(path.join(buildDir, 'windpeek.bin'))
const digest = createHash('sha256').update(firmware).digest('hex')
const metadata = JSON.stringify({ size: firmware.length, sha256: digest })

const server = await createServer({
  root: webDir,
  server: { host: '127.0.0.1', port: 4186, strictPort: true },
  plugins: [{
    name: 'local-e1003-firmware',
    configureServer(vite) {
      vite.middlewares.use((request, response, next) => {
        if (request.url === '/__local-e1003/meta') {
          response.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' })
          response.end(metadata)
        } else if (request.url === '/__local-e1003/app.bin') {
          response.writeHead(200, {
            'Content-Type': 'application/octet-stream',
            'Content-Length': firmware.length,
            'Cache-Control': 'no-store',
          })
          response.end(firmware)
        } else next()
      })
    },
  }],
})
await server.listen()
console.log('\nOpen in Dia: http://127.0.0.1:4186/dev/e1003-flash.html')
console.log(`E1003 app: ${firmware.length} bytes, SHA-256 ${digest}`)
