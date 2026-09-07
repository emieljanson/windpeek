export function isConfiguratorLocation(location = window.location) {
  const params = new URLSearchParams(location.search)
  return params.has('configure') || params.has('devicePreview') || params.has('installerDemo')
}
