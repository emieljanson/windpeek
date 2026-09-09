const BASE_HEADERS = {
  'Access-Control-Allow-Origin': '*',
  'Cache-Control': 'no-store',
}

const isCoordinateValue = (value) => (
  typeof value === 'number'
  || (typeof value === 'string' && value.trim() !== '')
)

const toCoordinates = (cf) => {
  if (!cf || !isCoordinateValue(cf.latitude) || !isCoordinateValue(cf.longitude)) {
    return null
  }

  const latitude = Number(cf.latitude)
  const longitude = Number(cf.longitude)

  if (
    !Number.isFinite(latitude)
    || !Number.isFinite(longitude)
    || latitude < -90
    || latitude > 90
    || longitude < -180
    || longitude > 180
  ) {
    return null
  }

  return { latitude, longitude }
}

const emptyResponse = (status, headers = {}) => new Response(null, {
  status,
  headers: { ...BASE_HEADERS, ...headers },
})

export default {
  fetch(request) {
    if (request.method === 'OPTIONS') {
      return emptyResponse(204, {
        'Access-Control-Allow-Headers': 'Content-Type',
        'Access-Control-Allow-Methods': 'GET, OPTIONS',
      })
    }

    if (request.method !== 'GET') {
      return emptyResponse(405, {
        Allow: 'GET, OPTIONS',
      })
    }

    const coordinates = toCoordinates(request.cf)
    if (!coordinates) {
      return emptyResponse(204)
    }

    const countryCode = String(request.cf?.country ?? '').toUpperCase()
    const location = /^[A-Z]{2}$/.test(countryCode) ? { ...coordinates, countryCode } : coordinates
    return Response.json(location, {
      headers: BASE_HEADERS,
    })
  },
}
