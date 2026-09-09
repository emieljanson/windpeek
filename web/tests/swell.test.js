import { beforeEach, describe, expect, it } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import { normalizeSwell, fetchOpenMeteoSwell, swellDashboardForecast } from '../src/forecast/openMeteoSwell'
import { createRendererInput } from '../src/renderer/rendererInput'
import { useConfiguratorStore } from '../src/stores/configurator'
import { brouwersdamForecast } from '../src/fixtures/brouwersdam'
const spot = { id: 'brouwersdam', name: 'Brouwersdam', latitude: 51.76, longitude: 3.86, timezone: 'Europe/Amsterdam' }
function response() {
  const time = Array.from({ length: 5 }, (_, d) => ['08','11','14','17','20'].map(h => `2026-09-${String(d+8).padStart(2,'0')}T${h}:00`)).flat()
  return { timezone: spot.timezone, hourly_units: { swell_wave_height: 'm', swell_wave_period: 's', swell_wave_direction: '°' }, hourly: { time, swell_wave_height: time.map(() => 1.24), swell_wave_period: time.map(() => 9.35), swell_wave_direction: time.map(() => 270) } }
}
describe('live swell preview', () => {
  beforeEach(() => setActivePinia(createPinia()))
  it('retains hourly graph data while keeping five labelled samples', () => {
    const r = response()
    r.hourly.time = Array.from({ length: 24 }, (_, hour) => `2026-09-08T${String(hour).padStart(2, '0')}:00`)
    r.hourly.swell_wave_height = Array(24).fill(1.2)
    r.hourly.swell_wave_period = Array(24).fill(9)
    r.hourly.swell_wave_direction = Array(24).fill(270)
    const swell = normalizeSwell(r, spot)
    expect(swell.hourly).toHaveLength(24)
    expect(swell.samples.map((sample) => sample.time)).toEqual(['08', '11', '14', '17', '20'])
  })
  it('retains secondary swell and ignores absent or incompatible secondary data', () => {
    const r = response()
    const n = r.hourly.time.length
    Object.assign(r.hourly_units, { secondary_swell_wave_height: 'm', secondary_swell_wave_period: 's', secondary_swell_wave_direction: '°' })
    Object.assign(r.hourly, { secondary_swell_wave_height: Array(n).fill(0.45), secondary_swell_wave_period: Array(n).fill(7.2), secondary_swell_wave_direction: Array(n).fill(270) })
    expect(normalizeSwell(r, spot).samples[0]).toMatchObject({ secondaryHeightCm: 45, secondaryPeriodTenths: 72, secondaryDestinationDegrees: 90 })
    r.hourly.secondary_swell_wave_height[0] = null
    expect(normalizeSwell(r, spot).samples[0].secondaryHeightCm).toBe(-1)
    r.hourly_units.secondary_swell_wave_height = 'ft'
    expect(normalizeSwell(r, spot).samples[1].secondaryHeightCm).toBe(-1)
    expect(normalizeSwell(response(), spot).samples[0].secondaryHeightCm).toBe(-1)
  })

  it('allows all module sizes independently, including an empty dashboard', () => {
    const store = useConfiguratorStore()
    store.swellStatus = 'ready'
    for (const wind of ['off', 'small', 'large']) {
      for (const swell of ['off', 'small', 'large']) {
        store.setModuleSize('wind', wind)
        store.setModuleSize('swell', swell)
        expect(store.windSize).toBe(wind)
        expect(store.swellSize).toBe(swell)
        expect(store.swellFocus).toBe(swell !== 'off')
      }
    }
    expect(store.setModuleSize('swell', 'invalid')).toBe(false)
  })
  it('keeps zero and partial data distinct and converts source direction once', () => {
    const r=response();r.hourly.swell_wave_height[0]=0;r.hourly.swell_wave_height[1]=null;r.hourly.swell_wave_period[2]=-2
    const s=normalizeSwell(r,spot)
    expect(s.samples[0]).toMatchObject({heightCm:0,periodTenths:94,destinationDegrees:90})
    expect(s.samples[1]).toMatchObject({heightCm:-1,periodTenths:94})
    expect(s.samples[2].periodTenths).toBe(-1)
  })
  it('rejects incompatible units and unordered time arrays', () => {
    const r=response();r.hourly_units.swell_wave_height='ft'
    expect(()=>normalizeSwell(r,spot)).toThrow()
    r.hourly_units.swell_wave_height='m';r.hourly.time.reverse()
    expect(()=>normalizeSwell(r,spot)).toThrow()
  })
  it('requests swell independently from tide', async () => {
    let request
    await fetchOpenMeteoSwell(spot,{fetchImpl:async(url,options)=>{request={url,options};return {ok:true,json:async()=>response()}}})
    expect(request.url).toContain('swell_wave_height');expect(request.url).not.toContain('sea_level')
    expect(request.options.signal).toBeInstanceOf(AbortSignal)
  })
  it('builds the live calendar without showing old demo wind as current', () => {
    const swell=normalizeSwell(response(),spot)
    const f=swellDashboardForecast(brouwersdamForecast,swell)
    expect(f.days[0].localDate).toBe('2026-09-08')
    expect(f.days[0].samples.every(s=>!s.available)).toBe(true)
    const input=createRendererInput(brouwersdamForecast,{swellFocus:true,swell,threshold:17,showThreshold:true})
    expect(input.displayMode).toBe(2)
    expect(input.days[0].samples[0]).toMatchObject({swellHeightCm:124,swellPeriodTenths:94,available:false})
  })
  it('ignores an older request resolving after a new one', async () => {
    const store=useConfiguratorStore();let resolve
    const pending=store.refreshSwell({fetcher:()=>new Promise(r=>{resolve=r})})
    const latest=normalizeSwell(response(),spot)
    await store.refreshSwell({fetcher:async()=>latest});resolve({...latest,available:false});await pending
    expect(store.swellStatus).toBe('ready')
  })
  it('reports request failure without falling back to fictional values',async()=>{
    const store=useConfiguratorStore();await store.refreshSwell({fetcher:async()=>{throw new Error('offline')}})
    expect(store.swellStatus).toBe('failed');expect(store.swell).toBeNull()
  })
})

describe('automatic GFS resolution', () => {
  it('keeps the selected forecast when its fallback request fails', async () => {
    const result = await fetchOpenMeteoSwell(spot, { fetchImpl: async url => {
      if (new URL(url).searchParams.get('models') === 'ncep_gfswave025') throw new Error('offline')
      return { ok: true, json: async () => response() }
    } })
    expect(result.model).toBe('best_match')
    expect(result.samples).toHaveLength(25)
    expect(result.samples.every(sample => sample.heightCm === 124)).toBe(true)
  })
  it('uses real global swell when MFWAM has no coverage at the spot', async () => {
    const result = await fetchOpenMeteoSwell(spot, { model: 'meteofrance_wave', fetchImpl: async url => {
      const r = response()
      if (new URL(url).searchParams.get('models') === 'meteofrance_wave') {
        for (const field of ['swell_wave_height', 'swell_wave_period', 'swell_wave_direction']) r.hourly[field].fill(null)
      }
      return { ok: true, json: async () => r }
    } })
    expect(result.available).toBe(true)
    expect(result.model).toBe('meteofrance_wave')
    expect(result.samples).toHaveLength(25)
    expect(result.samples.every(sample => sample.heightCm === 124 && sample.periodTenths === 94)).toBe(true)
  })
  it('fills the five-day Best Match forecast when the provider ends early', async () => {
    const result = await fetchOpenMeteoSwell(spot, { fetchImpl: async url => {
      const r = response()
      if (new URL(url).searchParams.get('models') === 'best_match') {
        r.hourly.swell_wave_height.fill(2)
        for (const field of ['swell_wave_height', 'swell_wave_period', 'swell_wave_direction']) {
          r.hourly[field].fill(null, 16)
        }
      }
      return { ok: true, json: async () => r }
    } })
    expect(result.model).toBe('best_match')
    expect(result.samples[0].heightCm).toBe(200)
    expect(result.samples.slice(16).every(sample => sample.heightCm === 124 && sample.periodTenths === 94)).toBe(true)
  })
  it('fills missing high-resolution hours with complete lower-resolution samples', async () => {
    const requested = []
    const result = await fetchOpenMeteoSwell(spot, { model: 'ncep_gfswave025', fetchImpl: async url => {
      const model = new URL(url).searchParams.get('models'); requested.push(model)
      const r = response()
      if (model === 'ncep_gfswave016') {
        r.hourly.swell_wave_height.fill(2)
        r.hourly.swell_wave_period[1] = null
        for (const key of Object.keys(r.hourly)) r.hourly[key] = r.hourly[key].slice(0, 10)
      }
      return { ok: true, json: async () => r }
    } })
    expect(requested.sort()).toEqual(['ncep_gfswave016', 'ncep_gfswave025'])
    expect(result.samples).toHaveLength(25)
    expect(result.samples[0].heightCm).toBe(200)
    expect(result.samples[1]).toMatchObject({heightCm:124,periodTenths:94})
    expect(result.samples[24].heightCm).toBe(124)
  })
  it('uses global GFS outside the finer grid and keeps the saved model identity', async () => {
    const requested = []
    const result = await fetchOpenMeteoSwell({...spot, latitude:-34}, { model:'ncep_gfswave025', fetchImpl:async url => {
      requested.push(new URL(url).searchParams.get('models'))
      return {ok:true,json:async()=>response()}
    } })
    expect(requested).toEqual(['ncep_gfswave025'])
    expect(result.model).toBe('ncep_gfswave025')
  })
  it('retains the global forecast when the finer request fails', async () => {
    const result = await fetchOpenMeteoSwell(spot, { model:'ncep_gfswave025', fetchImpl:async url => {
      if (url.includes('models=ncep_gfswave016')) throw new Error('unavailable')
      return {ok:true,json:async()=>response()}
    } })
    expect(result.available).toBe(true)
    expect(result.samples[0].heightCm).toBe(124)
  })
})

it('fills EWAM with global DWD swell while retaining one trace and the selected model', async () => {
  const requested = []
  const result = await fetchOpenMeteoSwell(spot, {model:'dwd_ewam',fetchImpl:async url => {
    const params = new URL(url).searchParams
    const model = params.get('models'); requested.push(model)
    expect(params.get('hourly')).not.toContain('secondary')
    const r = response()
    if (model === 'dwd_ewam') {
      r.hourly.swell_wave_height.fill(2)
      for (const key of Object.keys(r.hourly)) r.hourly[key] = r.hourly[key].slice(0, 10)
    }
    return {ok:true,json:async()=>r}
  }})
  expect(requested).toEqual(['dwd_ewam','dwd_gwam'])
  expect(result.model).toBe('dwd_ewam')
  expect(result.samples).toHaveLength(25)
  expect(result.samples[0].heightCm).toBe(200)
  expect(result.samples[24].heightCm).toBe(124)
  expect(result.samples.every(sample=>sample.secondaryHeightCm === -1)).toBe(true)
})
