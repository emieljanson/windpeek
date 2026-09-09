export const MODULE_IDS = Object.freeze(['wind', 'swell', 'weather', 'temperature', 'tide'])
export const MODULE_SIZES = Object.freeze(['off', 'small', 'large'])
export function validModuleOrder(order) {
  return Array.isArray(order) && order.length === MODULE_IDS.length &&
    new Set(order).size === MODULE_IDS.length && order.every((id) => MODULE_IDS.includes(id))
}
