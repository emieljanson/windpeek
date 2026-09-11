const variants = {
  wind: {
    id: 'wind', name: 'Windpeek',
    title: 'The always-on wind forecast for your favorite spot',
    intro: 'Windpeek turns a reTerminal into an e-ink display that keeps the next five days visible at home.',
    story: 'Wind forecasts change. You check once, it looks like nothing, and later discover it turned into a great session. Windpeek keeps the forecast in sight, so you catch the change instead of hearing about it afterwards.',
    description: 'Your five-day wind forecast on an always-on e-ink display. Choose your spot, models and screen layout, with waves, weather and tide alongside.',
  },
  swell: {
    id: 'swell', name: 'Windpeek',
    title: 'The always-on swell forecast for your favorite break',
    intro: 'Windpeek puts five days of swell, wind and tide on a reTerminal e-ink display at home.',
    story: 'A swell arrives, the wind turns offshore, and a window opens at your favorite break. Windpeek keeps the forecast in sight, so you can plan your next session.',
    description: 'Your five-day swell forecast on an always-on e-ink display. See swell height, period and direction alongside wind and tide.',
  },
}

export function siteVariant(location = globalThis.location) {
  const hostname = location?.hostname ?? ''
  const requested = new URLSearchParams(location?.search).get('site')
  if (requested === 'wind' || requested === 'swell') return variants[requested]
  if (/\/swell\/?$/.test(location?.pathname ?? '')) return variants.swell
  return variants[hostname === 'swellpeek.com' || hostname === 'www.swellpeek.com' ? 'swell' : 'wind']
}

export function siteDisplayDefaults(variant = siteVariant()) {
  const swell = variant.id === 'swell'
  return {
    windSize: swell ? 'small' : 'large', swellSize: swell ? 'large' : 'off',
    moduleOrder: swell ? ['swell', 'wind', 'weather', 'temperature', 'tide'] : ['wind', 'swell', 'weather', 'temperature', 'tide'],
    showWeather: true, showTemperature: false, showTide: true, showThreshold: !swell,
  }
}

export function configuratorLink(location = globalThis.location) {
  const params = new URLSearchParams({ configure: '', site: siteVariant(location).id })
  return `?${params}`
}
