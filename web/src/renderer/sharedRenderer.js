import {
  RENDERER_CONTRACT_VERSION,
  RENDERER_HEIGHT,
  RENDERER_PALETTE_BYTES,
  RENDERER_PREVIEW_DIMENSIONS,
  RENDERER_DISPLAYS,
  RENDERER_RGBA_BYTES,
  RENDERER_TEXT_CAPACITIES,
  RENDERER_WIDTH,
  textFitsRenderer,
} from './contract'
import { publicAssetUrl } from '../assets/publicAssetUrl'

export {
  RENDERER_CONTRACT_VERSION,
  RENDERER_HEIGHT,
  RENDERER_PALETTE_BYTES,
  RENDERER_RGBA_BYTES,
  RENDERER_WIDTH,
} from './contract'

export const RENDERER_LOAD_TIMEOUT_MS = 10_000

const EXPECTED_EXPORTS = [
  'memory',
  'wind_wasm_contract_version',
  'wind_wasm_width',
  'wind_wasm_height',
  'wind_wasm_palette_bytes',
  'wind_wasm_scratch_ptr',
  'wind_wasm_scratch_capacity',
  'wind_wasm_output_ptr',
  'wind_wasm_preview_output_ptr',
  'wind_wasm_preview_bytes',
  'wind_wasm_preview_width',
  'wind_wasm_preview_height',
  'wind_wasm_reset',
  'wind_wasm_set_metadata_field',
  'wind_wasm_set_status',
  'wind_wasm_set_display_rows',
  'wind_wasm_set_preferences',
  'wind_wasm_set_day_field',
  'wind_wasm_set_sample_label',
  'wind_wasm_set_sample_values',
  'wind_wasm_set_swell_sample',
  'wind_wasm_set_modules',
  'wind_wasm_set_module_order',
  'wind_wasm_set_swell_hour',
  'wind_wasm_set_secondary_swell_hour',
  'wind_wasm_set_secondary_swell_sample',
  'wind_wasm_set_tide_sample',
  'wind_wasm_set_tide_extremum',
  'wind_wasm_render',
  'wind_wasm_render_preview',
]

export class SharedRendererError extends Error {
  constructor(code, message, options) {
    super(message, options)
    this.name = 'SharedRendererError'
    this.code = code
  }
}

function fail(code, message, cause) {
  throw new SharedRendererError(code, message, cause ? { cause } : undefined)
}

function requireInteger(value, name) {
  if (!Number.isInteger(value)) fail('INVALID_INPUT', `${name} must be an integer`)
  return value
}

function requireString(value, name, capacity) {
  if (typeof value !== 'string') fail('INVALID_INPUT', `${name} must be a string`)
  if (value.includes('\0')) fail('INVALID_INPUT', `${name} contains an invalid character`)
  if (capacity && !textFitsRenderer(value, capacity)) {
    fail('INVALID_INPUT', `${name} is longer than the shared render contract allows`)
  }
  return value
}

function requireFlag(value, name) {
  if (typeof value !== 'boolean' && value !== 0 && value !== 1) {
    fail('INVALID_INPUT', `${name} must be true or false`)
  }
}

function validateInput(input) {
  if (!input || typeof input !== 'object') fail('INVALID_INPUT', 'Renderer input is required')
  if (input.version !== RENDERER_CONTRACT_VERSION) {
    fail('INCOMPATIBLE_CONTRACT', `Renderer input contract ${input.version ?? 'missing'} is not supported`)
  }
  for (const name of ['spotName', 'provider', 'updatedTime']) {
    requireString(input[name], name, RENDERER_TEXT_CAPACITIES[name])
  }
  for (const name of ['state', 'ageHours', 'batteryPercent', 'displayMode', 'thresholdKt']) {
    requireInteger(input[name], name)
  }
  requireFlag(input.refreshFailed, 'refreshFailed')
  for (const name of ['showWeather', 'showTemperature', 'showTide', 'showDedicatedFooter', 'tideAvailable']) {
    requireFlag(input[name], name)
  }
  requireFlag(input.use24Hour, 'use24Hour')
  requireFlag(input.temperatureFahrenheit, 'temperatureFahrenheit')
  if (!Array.isArray(input.tideSamples) || input.tideSamples.length > 121) {
    fail('INVALID_INPUT', 'Renderer input tideSamples must contain at most 121 samples')
  }
  if (input.tideAvailable && input.tideSamples.length < 2) {
    fail('INVALID_INPUT', 'Available tide data must contain at least two samples')
  }
  input.tideSamples.forEach((sample, index) => {
    for (const name of ['dayIndex', 'localHour', 'seaLevelMm']) {
      requireInteger(sample?.[name], `tideSamples[${index}].${name}`)
    }
    requireFlag(sample?.available, `tideSamples[${index}].available`)
  })
  if (!Array.isArray(input.tideExtrema) || input.tideExtrema.length > 32) {
    fail('INVALID_INPUT', 'Renderer input tideExtrema must contain at most 32 extrema')
  }
  input.tideExtrema.forEach((extremum, index) => {
    for (const name of ['dayIndex', 'localHour', 'localMinute', 'seaLevelMm']) {
      requireInteger(extremum?.[name], `tideExtrema[${index}].${name}`)
    }
    requireFlag(extremum?.isHigh, `tideExtrema[${index}].isHigh`)
    requireFlag(extremum?.available, `tideExtrema[${index}].available`)
  })
  if (!Array.isArray(input.days) || input.days.length !== 5) {
    fail('INVALID_INPUT', 'Renderer input must contain exactly five days')
  }
  input.days.forEach((day, dayIndex) => {
    requireString(day?.day, `days[${dayIndex}].day`, RENDERER_TEXT_CAPACITIES.day)
    requireString(day?.date, `days[${dayIndex}].date`, RENDERER_TEXT_CAPACITIES.date)
    if (!Array.isArray(day.samples) || day.samples.length !== 5) {
      fail('INVALID_INPUT', `days[${dayIndex}] must contain exactly five samples`)
    }
    day.samples.forEach((sample, sampleIndex) => {
      requireString(sample?.time, `days[${dayIndex}].samples[${sampleIndex}].time`, RENDERER_TEXT_CAPACITIES.time)
      for (const name of ['sustainedKt', 'gustKt', 'destinationDegrees', 'weather', 'temperatureTenthsC']) {
        requireInteger(sample[name], `days[${dayIndex}].samples[${sampleIndex}].${name}`)
      }
      requireFlag(sample.available, `days[${dayIndex}].samples[${sampleIndex}].available`)
      requireFlag(sample.temperatureAvailable, `days[${dayIndex}].samples[${sampleIndex}].temperatureAvailable`)
    })
  })
}

class SharedRenderer {
  #exports
  #encoder = new TextEncoder()

  constructor(exports) {
    this.#exports = exports
    this.width = exports.wind_wasm_width()
    this.height = exports.wind_wasm_height()
    this.paletteBytes = exports.wind_wasm_palette_bytes()
  }

  #assertActive() {
    if (!this.#exports) fail('DISPOSED', 'The shared renderer has been disposed')
  }

  #call(name, ...args) {
    this.#assertActive()
    const result = this.#exports[name](...args)
    if (result !== 0) fail('INVALID_INPUT', `The canonical renderer rejected ${name}`)
  }

  #writeString(value, name) {
    const encoded = this.#encoder.encode(requireString(value, name))
    const pointer = this.#exports.wind_wasm_scratch_ptr()
    const capacity = this.#exports.wind_wasm_scratch_capacity()
    if (encoded.byteLength >= capacity) {
      fail('INVALID_INPUT', `${name} is longer than the shared render contract allows`)
    }
    const target = new Uint8Array(this.#exports.memory.buffer, pointer, capacity)
    target.fill(0)
    target.set(encoded)
  }

  #copyOutput(pointerExport, byteLength) {
    const pointer = this.#exports[pointerExport]()
    if (!pointer || pointer + byteLength > this.#exports.memory.buffer.byteLength) {
      fail('RENDER_FAILED', 'The canonical renderer did not publish a complete bitmap')
    }
    return new Uint8Array(this.#exports.memory.buffer, pointer, byteLength).slice()
  }

  #prepareInput(input) {
    this.#assertActive()
    validateInput(input)
    this.#call('wind_wasm_reset', input.version)

    ;['spotName', 'provider', 'updatedTime'].forEach((name, field) => {
      this.#writeString(input[name], name)
      this.#call('wind_wasm_set_metadata_field', field)
    })
    this.#call(
      'wind_wasm_set_status',
      input.state,
      input.refreshFailed ? 1 : 0,
      input.ageHours,
      input.batteryPercent,
      input.displayMode,
      input.thresholdKt,
    )
    this.#call(
      'wind_wasm_set_display_rows',
      input.showWeather ? 1 : 0,
      input.showTemperature ? 1 : 0,
      input.showTide ? 1 : 0,
      input.tideAvailable ? 1 : 0,
    )
    this.#call(
      'wind_wasm_set_preferences',
      input.use24Hour ? 1 : 0,
      input.temperatureFahrenheit ? 1 : 0,
      input.showDedicatedFooter ? 1 : 0,
    )

    const windSize = input.windSize ?? (input.swellFocus ? 1 : 2)
    const swellSize = input.swellSize ?? (input.swellFocus ? 2 : 0)
    if (input.windSize !== undefined || input.swellSize !== undefined || windSize !== 2 || swellSize !== 0) {
      this.#call('wind_wasm_set_modules', requireInteger(windSize, 'windSize'), requireInteger(swellSize, 'swellSize'))
    }
    if (input.moduleOrder) {
      if (!Array.isArray(input.moduleOrder) || input.moduleOrder.length !== 5 || new Set(input.moduleOrder).size !== 5) fail('INVALID_INPUT', 'Invalid module order')
      this.#call('wind_wasm_set_module_order', ...input.moduleOrder.map(id => requireInteger(id, 'moduleOrder')))
    }
    if (input.swellHourly != null && !Array.isArray(input.swellHourly)) fail('INVALID_INPUT', 'swellHourly must be an array')
    for (const sample of input.swellHourly ?? []) {
      if (!sample || typeof sample !== 'object') fail('INVALID_INPUT', 'Invalid swell hourly sample')
      this.#call('wind_wasm_set_swell_hour', requireInteger(sample.dayIndex, 'dayIndex'),
        requireInteger(sample.hour, 'hour'), requireInteger(sample.heightCm, 'heightCm'))
      this.#call('wind_wasm_set_secondary_swell_hour', requireInteger(sample.dayIndex, 'dayIndex'),
        requireInteger(sample.hour, 'hour'), requireInteger(sample.secondaryHeightCm ?? -1, 'secondaryHeightCm'))
    }
    input.days.forEach((day, dayIndex) => {
      for (const [field, name] of ['day', 'date'].entries()) {
        this.#writeString(day[name], `days[${dayIndex}].${name}`)
        this.#call('wind_wasm_set_day_field', dayIndex, field)
      }
      day.samples.forEach((sample, sampleIndex) => {
        if (swellSize > 0) this.#call('wind_wasm_set_swell_sample', dayIndex, sampleIndex,
          requireInteger(sample.swellHeightCm, 'swellHeightCm'),
          requireInteger(sample.swellPeriodTenths, 'swellPeriodTenths'),
          requireInteger(sample.swellDestinationDegrees, 'swellDestinationDegrees'))
        if (swellSize > 0) this.#call('wind_wasm_set_secondary_swell_sample', dayIndex, sampleIndex,
          requireInteger(sample.secondarySwellHeightCm ?? -1, 'secondarySwellHeightCm'),
          requireInteger(sample.secondarySwellPeriodTenths ?? -1, 'secondarySwellPeriodTenths'),
          requireInteger(sample.secondarySwellDestinationDegrees ?? -1, 'secondarySwellDestinationDegrees'))
        this.#writeString(sample.time, `days[${dayIndex}].samples[${sampleIndex}].time`)
        this.#call('wind_wasm_set_sample_label', dayIndex, sampleIndex)
        this.#call(
          'wind_wasm_set_sample_values',
          dayIndex,
          sampleIndex,
          sample.sustainedKt,
          sample.gustKt,
          sample.destinationDegrees,
          sample.available ? 1 : 0,
          sample.weather,
          sample.temperatureTenthsC,
          sample.temperatureAvailable ? 1 : 0,
        )
      })
    })
    input.tideSamples.forEach((sample, tideIndex) => {
      this.#call(
        'wind_wasm_set_tide_sample',
        tideIndex,
        sample.dayIndex,
        sample.localHour,
        sample.seaLevelMm,
        sample.available ? 1 : 0,
      )
    })
    input.tideExtrema.forEach((extremum, extremumIndex) => {
      this.#call(
        'wind_wasm_set_tide_extremum',
        extremumIndex,
        extremum.dayIndex,
        extremum.localHour,
        extremum.localMinute,
        extremum.seaLevelMm,
        extremum.isHigh ? 1 : 0,
        extremum.available ? 1 : 0,
      )
    })
  }

  render(input) {
    this.#prepareInput(input)
    this.#call('wind_wasm_render')
    return this.#copyOutput('wind_wasm_output_ptr', this.paletteBytes)
  }

  renderPreviewForDisplay(input, display) {
    const expected = RENDERER_PREVIEW_DIMENSIONS[display]
    if (!expected) fail('INVALID_INPUT', 'Unknown renderer display')
    this.#prepareInput(input)
    this.#call('wind_wasm_render_preview', display)
    const width = this.#exports.wind_wasm_preview_width()
    const height = this.#exports.wind_wasm_preview_height()
    const byteLength = this.#exports.wind_wasm_preview_bytes()
    if (width !== expected.width || height !== expected.height ||
        byteLength !== width * height * 4) {
      fail('RENDER_FAILED', 'The renderer published invalid preview dimensions')
    }
    return {
      data: this.#copyOutput('wind_wasm_preview_output_ptr', byteLength),
      width,
      height,
    }
  }

  renderPreview(input) {
    return this.renderPreviewForDisplay(input, RENDERER_DISPLAYS.E1002_SPECTRA6).data
  }

  dispose() {
    this.#exports = null
  }
}

async function getWasmBytes({ wasmBytes, wasmUrl, fetchImpl, signal }) {
  if (wasmBytes) return wasmBytes
  const response = await fetchImpl(wasmUrl, { signal })
  if (!response?.ok) throw new Error(`HTTP ${response?.status ?? 'error'}`)
  return response.arrayBuffer()
}

export async function loadSharedRenderer({
  wasmBytes,
  wasmUrl = publicAssetUrl('renderer/wind-renderer.wasm'),
  fetchImpl = globalThis.fetch,
  instantiate = WebAssembly.instantiate,
  timeoutMs = RENDERER_LOAD_TIMEOUT_MS,
} = {}) {
  const controller = wasmBytes ? null : new AbortController()
  let didTimeout = false
  const timeout = controller && setTimeout(() => {
    didTimeout = true
    controller.abort()
  }, timeoutMs)
  try {
    if (!wasmBytes && typeof fetchImpl !== 'function') throw new Error('Fetch is unavailable')
    const bytes = await getWasmBytes({ wasmBytes, wasmUrl, fetchImpl, signal: controller?.signal })
    const result = await instantiate(bytes, {})
    const instance = result instanceof WebAssembly.Instance ? result : result.instance
    const exports = instance?.exports
    if (!exports) throw new Error('WebAssembly instance has no exports')
    const missingExport = EXPECTED_EXPORTS.find((name) => !(name in exports))
    if (missingExport) throw new Error(`WebAssembly export ${missingExport} is missing`)
    exports._initialize?.()

    const version = exports.wind_wasm_contract_version()
    const width = exports.wind_wasm_width()
    const height = exports.wind_wasm_height()
    const paletteBytes = exports.wind_wasm_palette_bytes()
    if (version !== RENDERER_CONTRACT_VERSION) {
      fail('INCOMPATIBLE_RENDERER', `Renderer contract ${version} is not supported`)
    }
    if (
      width !== RENDERER_WIDTH ||
      height !== RENDERER_HEIGHT ||
      paletteBytes !== RENDERER_PALETTE_BYTES
    ) {
      fail('INCOMPATIBLE_RENDERER', 'Renderer dimensions do not match the Windpeek display')
    }
    return new SharedRenderer(exports)
  } catch (error) {
    if (error instanceof SharedRendererError) throw error
    if (didTimeout) fail('LOAD_TIMEOUT', 'The Windpeek screen renderer took too long to load', error)
    fail('LOAD_FAILED', 'The Windpeek screen renderer could not be loaded', error)
  } finally {
    if (timeout) clearTimeout(timeout)
  }
}
