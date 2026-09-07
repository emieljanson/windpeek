import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import { nextTick } from 'vue'

import WindScoutSettings from '../src/components/WindScoutSettings.vue'
import ReTerminalHelpDialog from '../src/components/ReTerminalHelpDialog.vue'
import SpotCreationDialog from '../src/components/SpotCreationDialog.vue'
import SettingCombobox from '../src/components/settings/SettingCombobox.vue'
import SettingSelect from '../src/components/settings/SettingSelect.vue'
import { useConfiguratorStore } from '../src/stores/configurator'

let wrapper

function mountSettings(props = {}) {
  wrapper = mount(WindScoutSettings, { props, attachTo: document.body })
  return wrapper
}

function rowControl(label) {
  const row = wrapper.findAll('.setting-row')
    .find((candidate) => candidate.get('.setting-row__label').text() === label)
  if (!row) throw new Error(`Missing settings row: ${label}`)
  return row
}

function bodyOption(label) {
  return [...document.body.querySelectorAll('[role="option"]')]
    .find((candidate) => candidate.textContent.includes(label))
}

describe('WindScout settings panel', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
  })

  afterEach(() => {
    wrapper?.unmount()
    wrapper = undefined
    document.body.innerHTML = ''
  })

  it('uses the compact inspector hierarchy with the active spot ready to replace', async () => {
    mountSettings()

    await nextTick()

    expect(wrapper.findAll('.setting-section__title')).toHaveLength(0)
    expect(wrapper.findAll('.setting-row__label').map((label) => label.text())).toEqual([
      'reTerminal',
      'Wind model',
      'Wind threshold',
      'Weather',
      'Temperature',
      'Tide',
      'Legend',
    ])
    expect(wrapper.text()).not.toContain('Treatment')
    expect(wrapper.text()).not.toContain('Time format')
    const spotSearch = wrapper.get('.inspector-search input[role="combobox"]')
    expect(spotSearch.element.value).toBe('')
    expect(spotSearch.attributes('placeholder')).toBe('Search spot…')
    expect(document.activeElement).toBe(spotSearch.element)
    expect(spotSearch.element.selectionStart).toBe(0)
    expect(spotSearch.element.selectionEnd).toBe(0)
    expect(spotSearch.classes()).toContain('is-initial-focus')
    await spotSearch.trigger('blur')
    expect(spotSearch.classes()).not.toContain('is-initial-focus')
    expect(rowControl('Wind model').find('.setting-select__chevron').exists()).toBe(true)
  })

  it('keeps search empty when the store applies an automatic spot', async () => {
    const store = useConfiguratorStore()
    mountSettings()

    store.selectedSpotId = 'edam'
    await nextTick()

    expect(wrapper.get('.inspector-search input[role="combobox"]').element.value).toBe('')
    expect(store.hasUserSpotIntent).toBe(false)
  })

  it('filters local spots, commits only a supplied result, and announces no results', async () => {
    const store = useConfiguratorStore()
    store.selectedSpotId = 'edam'
    const selectSpot = vi.spyOn(store, 'selectSpot').mockResolvedValue(true)
    const markUserSpotIntent = vi.spyOn(store, 'markUserSpotIntent')
    mountSettings()
    const input = wrapper.get('.inspector-search input[role="combobox"]')

    await input.trigger('focus')
    await input.trigger('click')
    expect(document.body.querySelector('[role="listbox"]')).toBeNull()
    expect(document.body.textContent).not.toContain('No existing spots found')

    await input.setValue('b')
    expect(markUserSpotIntent).toHaveBeenCalledOnce()
    await nextTick()
    expect(document.body.querySelector('[role="listbox"]')).toBeNull()

    await input.setValue('bro')
    await nextTick()
    expect(bodyOption('Brouwersdam')).toBeDefined()
    expect(bodyOption('Edam')).toBeUndefined()

    bodyOption('Brouwersdam').click()
    await nextTick()
    expect(selectSpot).toHaveBeenCalledWith('brouwersdam')
    expect(markUserSpotIntent).toHaveBeenCalledTimes(3)
    expect(input.element.value).toBe('Brouwersdam')

    await input.setValue('nowhere')
    await nextTick()
    expect(document.body.textContent).not.toContain('No existing spots found')
    expect(selectSpot).toHaveBeenCalledTimes(1)
  })

  it('keeps compact mode focused on display choices without a device selector', async () => {
    const store = useConfiguratorStore()
    mountSettings({ compact: true })

    expect(wrapper.findComponent(SpotCreationDialog).exists()).toBe(false)
    expect(wrapper.get('.settings-shell').classes()).toContain('settings-shell--compact')
    expect(wrapper.find('.inspector-search').exists()).toBe(false)
    expect(wrapper.find('.inspector-divider').exists()).toBe(false)
    expect(wrapper.text()).not.toContain('Wind model')
    expect(wrapper.findAll('select.setting-select__native')).toHaveLength(0)
    expect(wrapper.findAll('.setting-select__trigger')).toHaveLength(0)
    expect(wrapper.find('[name="device"]').exists()).toBe(false)
    expect(wrapper.find('button[aria-label="About reTerminal devices"]').exists()).toBe(false)
    const pills = wrapper.findAll('.mobile-display-pill')
    expect(pills.map((pill) => pill.text())).toEqual(['Threshold', 'Weather', 'Temp', 'Tide'])
    expect(pills.map((pill) => pill.attributes('aria-pressed'))).toEqual(['false', 'true', 'false', 'false'])

    await pills[0].trigger('click')
    await pills[2].trigger('click')
    expect(store.selectedBoardId).toBe('seeedstudio_reterminal_e1002')
    expect(store.showThreshold).toBe(true)
    expect(store.temperatureChoice).toBe('celsius')
    expect(pills[0].attributes('aria-pressed')).toBe('true')
    expect(pills[2].attributes('aria-pressed')).toBe('true')
  })

  it('explains the reTerminal choices from the settings label', async () => {
    mountSettings()
    const help = wrapper.get('button[aria-label="About reTerminal devices"]')

    expect(help.text()).toBe('reTerminal')
    expect(wrapper.text()).not.toContain('?')
    expect(help.attributes('aria-haspopup')).toBe('dialog')
    expect(help.attributes('aria-expanded')).toBe('false')
    await help.trigger('click')
    await nextTick()

    expect(wrapper.findComponent(ReTerminalHelpDialog).props('open')).toBe(true)
    const dialog = document.body.querySelector('[role="dialog"]')
    expect(dialog?.getAttribute('aria-labelledby')).toBeTruthy()
    expect(dialog?.textContent).toContain('Windscout for reTerminal')
    expect(dialog?.textContent).toContain('Windscout only uses colour for the threshold line')
    expect(dialog?.textContent).toContain('Direct installation supports E1001, E1002 and E1003')
    expect(dialog?.textContent).toContain('E1001')
    expect(dialog?.textContent).toContain('7.3″ six-colour — E1002')
    expect(dialog?.textContent).toContain('10.3″ monochrome — E1003')
    expect(dialog?.textContent).not.toContain('touch')
    expect(dialog?.textContent).not.toContain('affiliate')
    const buyLinks = [...dialog.querySelectorAll('a')]
    const deviceImages = [...dialog.querySelectorAll('img')]
    expect(deviceImages.map((image) => image.getAttribute('src'))).toEqual([
      '/devices/previews/e1001.png',
      '/devices/previews/e1002.png',
      '/devices/previews/e1003.png',
    ])
    expect(deviceImages.every((image) => image.getAttribute('alt') === '')).toBe(true)
    expect(buyLinks.map((link) => link.textContent.trim().replace(/\s+/g, ' '))).toEqual([
      'Buy ~$70',
      'Buy ~$107',
      'Buy ~$160',
    ])
    expect(buyLinks.map((link) => link.getAttribute('aria-label'))).toEqual([
      'Buy reTerminal E1001, approximately $70',
      'Buy reTerminal E1002, approximately $107',
      'Buy reTerminal E1003, approximately $160',
    ])
    expect(buyLinks.every((link) => link.href.includes('sensecap_affiliate=UF4PmgK'))).toBe(true)
    expect(buyLinks.every((link) => link.rel === 'sponsored noopener noreferrer')).toBe(true)

    document.body.querySelector('button[aria-label="Close reTerminal help"]')?.click()
    await nextTick()
    expect(wrapper.findComponent(ReTerminalHelpDialog).props('open')).toBe(false)
  })

  it('opens custom spot creation from the typed add action and selects the saved spot', async () => {
    const store = useConfiguratorStore()
    const personalSpot = {
      id: 'personal-edam-harbour',
      name: 'Edam harbour',
      displayName: 'EDAM HARBOUR',
      latitude: 52.50673,
      longitude: 5.07729,
      timezone: 'Europe/Amsterdam',
      personal: true,
    }
    const addPersonalSpot = vi.spyOn(store, 'addPersonalSpot').mockReturnValue(personalSpot)
    const selectSpot = vi.spyOn(store, 'selectSpot').mockReturnValue(new Promise(() => {}))
    const markUserSpotIntent = vi.spyOn(store, 'markUserSpotIntent')
    mountSettings()
    const spotCombobox = wrapper.findComponent(SettingCombobox)

    spotCombobox.vm.$emit('update:searchTerm', 'Edam harbour')
    await nextTick()
    expect(spotCombobox.props('createActionLabel')).toBe('Add Edam harbour')
    spotCombobox.vm.$emit('create', 'Edam harbour')
    await nextTick()

    const dialog = wrapper.findComponent(SpotCreationDialog)
    expect(dialog.props('open')).toBe(true)
    expect(dialog.props('initialQuery')).toBe('Edam harbour')
    expect(markUserSpotIntent).toHaveBeenCalledOnce()
    const savedSpot = dialog.props('saveSpot')(personalSpot)
    await nextTick()

    expect(savedSpot).toBe(personalSpot)
    expect(addPersonalSpot).toHaveBeenCalledWith(personalSpot)
    expect(selectSpot).toHaveBeenCalledWith(personalSpot.id)
    expect(markUserSpotIntent).toHaveBeenCalledTimes(2)
    expect(spotCombobox.props('searchTerm')).toBe('Edam harbour')
  })

  it('offers the curated model list and selects a model through the store', async () => {
    const store = useConfiguratorStore()
    const selectModel = vi.spyOn(store, 'selectModel').mockResolvedValue(true)
    mountSettings()
    const modelSelect = rowControl('Wind model').findComponent(SettingSelect)

    expect(wrapper.get('.settings-shell').classes()).not.toContain('settings-shell--compact')
    expect(wrapper.findAll('select.setting-select__native')).toHaveLength(0)
    expect(wrapper.findAll('.setting-select__trigger')).toHaveLength(3)

    expect(modelSelect.props('options').map((option) => option.label)).toEqual([
      'Best Match', 'ECMWF IFS', 'DWD ICON', 'NOAA GFS',
    ])
    expect(modelSelect.props('options').map((option) => Boolean(option.separatorBefore))).toEqual([
      false, true, false, false,
    ])
    modelSelect.vm.$emit('update:modelValue', 'ncep_gfs_seamless')
    await nextTick()

    expect(selectModel).toHaveBeenCalledWith('ncep_gfs_seamless')
  })

  it('puts Best Match in its own section above local and global models', () => {
    const store = useConfiguratorStore()
    store.forecastsByModel.knmi_harmonie = { spotId: store.selectedSpotId }
    store.forecastsByModel.dmi_harmonie = { spotId: store.selectedSpotId }
    mountSettings()

    const modelOptions = rowControl('Wind model').findComponent(SettingSelect).props('options')
    expect(modelOptions.map((option) => option.label)).toEqual([
      'Best Match',
      'KNMI HARMONIE', 'DMI HARMONIE',
      'ECMWF IFS', 'DWD ICON', 'NOAA GFS',
    ])
    expect(modelOptions.map((option) => Boolean(option.separatorBefore))).toEqual([
      false, true, false, true, false, false,
    ])
  })

  it('reveals an exact threshold input and restores its last valid value', async () => {
    const store = useConfiguratorStore()
    mountSettings()
    const thresholdSwitch = rowControl('Wind threshold').get('[role="switch"]')

    expect(wrapper.find('input[type="number"]').exists()).toBe(false)
    await thresholdSwitch.trigger('click')
    expect(store.showThreshold).toBe(true)

    const thresholdInput = rowControl('Minimum wind').get('input[type="number"]')
    await thresholdInput.setValue('24')
    expect(store.threshold).toBe(24)

    await thresholdSwitch.trigger('click')
    expect(store.showThreshold).toBe(false)
    expect(wrapper.find('input[type="number"]').exists()).toBe(false)
    await thresholdSwitch.trigger('click')
    expect(rowControl('Minimum wind').get('input').element.value).toBe('24')
  })

  it('maps Temperature to Hide, Celsius, or Fahrenheit as one setting', async () => {
    const store = useConfiguratorStore()
    mountSettings()
    const temperatureSelect = rowControl('Temperature').findComponent(SettingSelect)

    expect(store.temperatureChoice).toBe('hide')
    expect(temperatureSelect.props('options').map((option) => option.label)).toEqual([
      'Hide', 'Celsius', 'Fahrenheit',
    ])
    temperatureSelect.vm.$emit('update:modelValue', 'fahrenheit')
    await nextTick()
    expect(store.temperatureChoice).toBe('fahrenheit')

    temperatureSelect.vm.$emit('update:modelValue', 'hide')
    await nextTick()
    expect(store.temperatureChoice).toBe('hide')
  })

  it('shows Tide effectively off while unavailable without losing its preference', async () => {
    const store = useConfiguratorStore()
    store.tide = { capability: 'available' }
    store.tideStatus = 'available'
    store.showTide = true
    mountSettings()

    const tideSwitch = rowControl('Tide').get('[role="switch"]')
    expect(tideSwitch.attributes('data-state')).toBe('checked')
    expect(tideSwitch.attributes('disabled')).toBeUndefined()

    store.tide = null
    store.tideStatus = 'failed'
    store.tideMessage = 'Could not check tide availability. Try again later.'
    await nextTick()

    expect(store.showTide).toBe(true)
    expect(store.effectiveShowTide).toBe(false)
    expect(tideSwitch.attributes('data-state')).toBe('unchecked')
    const tideRow = rowControl('Tide')
    const tooltip = tideRow.get('[role="tooltip"]')
    expect(tideRow.classes()).not.toContain('setting-row--disabled')
    expect(tideSwitch.attributes('disabled')).toBeUndefined()
    expect(tideSwitch.attributes('aria-disabled')).toBe('true')
    expect(tideSwitch.attributes('aria-describedby')).toBe(tooltip.attributes('id'))
    expect(tooltip.text()).toContain('Could not check')
    await tideSwitch.trigger('keydown', { key: ' ' })
    expect(tideSwitch.attributes('data-state')).toBe('unchecked')

    store.tide = { capability: 'available' }
    store.tideStatus = 'available'
    await nextTick()
    expect(tideSwitch.attributes('data-state')).toBe('checked')
    expect(tideSwitch.attributes('aria-disabled')).toBeUndefined()
  })

  it('announces forecast progress and keeps the demo badge outside the screen', () => {
    mountSettings()
    expect(wrapper.get('.forecast-status').attributes('aria-live')).toBe('polite')
    expect(wrapper.get('[data-testid="forecast-label"]').text()).toBe('Demo')
    expect(wrapper.text()).toContain('Loading current Brouwersdam weather')
  })
})
