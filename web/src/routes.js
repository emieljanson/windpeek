export function isConfiguratorLocation(location = window.location) {
  const params = new URLSearchParams(location.search)
  return params.has('configure') || params.has('devicePreview') || params.has('installerDemo')
}

export function pageBackgroundForLocation(location = window.location) {
  return isConfiguratorLocation(location) ? '#f3f5f7' : '#ffffff'
}
