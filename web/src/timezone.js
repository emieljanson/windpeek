export function validTimezone(timezone) {
  if (typeof timezone !== 'string' || !timezone) return false
  try {
    new Intl.DateTimeFormat('en', { timeZone: timezone }).format()
    return true
  } catch {
    return false
  }
}

export function deviceTimezone() {
  const timezone = new Intl.DateTimeFormat().resolvedOptions().timeZone
  return validTimezone(timezone) ? timezone : 'Etc/UTC'
}
