import 'maplibre-gl/dist/maplibre-gl.css'
import workerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?worker&url'

export async function createGeoapifyMap(container, {
  apiKey,
  center,
  zoom = 13,
  signal,
  onCenterChange = () => {},
}) {
  if (!container) throw new Error('The map container is unavailable.')
  if (!apiKey) throw new Error('Geoapify is not configured for this site.')
  const { Map, NavigationControl, setWorkerUrl } = await import('maplibre-gl')
  // Bundle the worker and its imports; its default sibling URL is lost when
  // Vite optimizes dependencies or names production chunks.
  setWorkerUrl(workerUrl)
  const map = new Map({
    container,
    style: `https://maps.geoapify.com/v1/styles/positron/style.json?apiKey=${encodeURIComponent(apiKey)}`,
    center: [center.longitude, center.latitude],
    zoom,
    attributionControl: { compact: true },
  })
  map.addControl(new NavigationControl({ showCompass: false }), 'top-right')
  let programmaticCenter = null
  const clearProgrammaticCenterOnUserMove = (event) => {
    if (event.originalEvent) programmaticCenter = null
  }
  const reportCenter = () => {
    if (programmaticCenter) {
      const value = programmaticCenter
      programmaticCenter = null
      onCenterChange(value)
      return
    }
    const value = map.getCenter()
    onCenterChange({ latitude: value.lat, longitude: value.lng })
  }
  map.on('movestart', clearProgrammaticCenterOnUserMove)
  map.on('moveend', reportCenter)
  await new Promise((resolve, reject) => {
    const cleanup = () => {
      map.off('load', loaded)
      map.off('error', failed)
      signal?.removeEventListener('abort', aborted)
    }
    const loaded = () => {
      cleanup()
      map.resize()
      const attribution = container.querySelector('.maplibregl-ctrl-attrib.maplibregl-compact')
      attribution?.classList.remove('maplibregl-compact-show')
      attribution?.removeAttribute('open')
      resolve()
    }
    const fail = (error) => {
      cleanup()
      map.off('moveend', reportCenter)
      map.remove()
      reject(error)
    }
    const failed = () => fail(new Error('The map could not be loaded.'))
    const aborted = () => fail(new DOMException('Aborted', 'AbortError'))
    map.once('load', loaded)
    map.once('error', failed)
    if (signal?.aborted) aborted()
    else signal?.addEventListener('abort', aborted, { once: true })
  })
  return {
    setCenter(nextCenter, { zoom: nextZoom = 13 } = {}) {
      programmaticCenter = { ...nextCenter }
      map.easeTo({
        center: [nextCenter.longitude, nextCenter.latitude],
        zoom: nextZoom,
        duration: 280,
      })
    },
    destroy() {
      map.off('movestart', clearProgrammaticCenterOnUserMove)
      map.off('moveend', reportCenter)
      map.remove()
    },
  }
}
