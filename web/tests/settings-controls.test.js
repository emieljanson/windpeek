import { describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'

import SettingRow from '../src/components/settings/SettingRow.vue'
import SettingSelect from '../src/components/settings/SettingSelect.vue'
import SettingCombobox from '../src/components/settings/SettingCombobox.vue'
import SettingSwitch from '../src/components/settings/SettingSwitch.vue'
import SettingNumberInput from '../src/components/settings/SettingNumberInput.vue'

const spots = [
  { value: 'edam', label: 'Edam' },
  { value: 'brouwersdam', label: 'Brouwersdam' },
]

describe('Windpeek setting controls', () => {
  it('associates a visible row label and description with a Select', async () => {
    const wrapper = mount({
      components: { SettingRow, SettingSelect },
      template: `
        <SettingRow label="Model" description="Forecast source">
          <SettingSelect model-value="best_match" :options="options" />
        </SettingRow>
      `,
      data: () => ({ options: [{ value: 'best_match', label: 'Best Match' }] }),
    }, { attachTo: document.body })

    const trigger = wrapper.get('[role="combobox"]')
    const label = wrapper.get('label')
    const description = wrapper.get('.setting-row__description')

    expect(trigger.attributes('aria-labelledby')).toBe(label.attributes('id'))
    expect(trigger.attributes('aria-describedby')).toBe(description.attributes('id'))
    expect(wrapper.find('.setting-select__chevron').exists()).toBe(true)
    wrapper.unmount()
  })

  it('uses one native, fully labelled select for compact settings', async () => {
    const onUpdate = vi.fn()
    const wrapper = mount({
      components: { SettingRow, SettingSelect },
      template: `
        <SettingRow label="Model" description="Forecast source">
          <SettingSelect
            model-value="best_match"
            :options="options"
            name="model"
            native
            @update:model-value="onUpdate"
          />
        </SettingRow>
      `,
      data: () => ({
        options: [
          { value: 'best_match', label: 'Best Match' },
          { value: 'ecmwf', label: 'ECMWF' },
          { value: 'gfs', label: 'GFS', disabled: true },
        ],
      }),
      methods: { onUpdate },
    })

    const select = wrapper.get('select')
    const label = wrapper.get('label')
    const description = wrapper.get('.setting-row__description')
    expect(wrapper.find('[role="combobox"].setting-select__trigger').exists()).toBe(false)
    expect(select.attributes('id')).toBe(label.attributes('for'))
    expect(select.attributes('aria-labelledby')).toBe(label.attributes('id'))
    expect(select.attributes('aria-describedby')).toBe(description.attributes('id'))
    expect(select.attributes('name')).toBe('model')
    expect(select.element.value).toBe('best_match')
    expect(select.findAll('option').map((option) => [option.text(), option.attributes('value')])).toEqual([
      ['Best Match', 'best_match'],
      ['ECMWF', 'ecmwf'],
      ['GFS', 'gfs'],
    ])
    expect(select.findAll('option')[2].attributes('disabled')).toBeDefined()

    await select.setValue('ecmwf')
    expect(onUpdate).toHaveBeenCalledWith('ecmwf')
  })

  it('keeps a compact native select genuinely disabled', () => {
    const wrapper = mount(SettingSelect, {
      props: {
        modelValue: 'best_match',
        options: [{ value: 'best_match', label: 'Best Match' }],
        native: true,
        disabled: true,
        ariaLabel: 'Model',
      },
    })

    expect(wrapper.get('select').attributes('disabled')).toBeDefined()
    expect(wrapper.get('select').attributes('aria-label')).toBe('Model')
  })

  it('announces disabled Select and Switch states through their native semantics', () => {
    const wrapper = mount({
      components: { SettingRow, SettingSelect, SettingSwitch },
      template: `
        <div>
          <SettingRow label="Model"><SettingSelect model-value="best_match" :options="options" disabled /></SettingRow>
          <SettingRow label="Tide"><SettingSwitch :model-value="false" disabled /></SettingRow>
        </div>
      `,
      data: () => ({ options: [{ value: 'best_match', label: 'Best Match' }] }),
    })

    const labels = wrapper.findAll('label')
    const select = wrapper.get('[role="combobox"]')
    const toggle = wrapper.get('[role="switch"]')
    expect(select.attributes('disabled')).toBeDefined()
    expect(toggle.attributes('disabled')).toBeDefined()
    expect(select.attributes('aria-labelledby')).toBe(labels[0].attributes('id'))
    expect(toggle.attributes('aria-labelledby')).toBe(labels[1].attributes('id'))
  })

  it('treats Show and Hide segments as explicit choices', async () => {
    const wrapper = mount(SettingSwitch, {
      props: { modelValue: true },
    })

    await wrapper.get('.setting-switch__segment--on').trigger('click')
    expect(wrapper.emitted('update:modelValue')).toBeUndefined()

    await wrapper.get('.setting-switch__segment--off').trigger('click')
    expect(wrapper.emitted('update:modelValue')?.at(-1)).toEqual([false])

    await wrapper.setProps({ modelValue: false })
    await wrapper.get('.setting-switch__segment--off').trigger('click')
    expect(wrapper.emitted('update:modelValue')).toHaveLength(1)

    await wrapper.get('.setting-switch__segment--on').trigger('click')
    expect(wrapper.emitted('update:modelValue')?.at(-1)).toEqual([true])
  })

  it('keeps a reasoned disabled switch focusable without changing its value', async () => {
    const wrapper = mount(SettingSwitch, {
      props: {
        modelValue: false,
        disabled: true,
        disabledReason: 'Tide is not available for this spot.',
        ariaLabel: 'Tide',
      },
      attachTo: document.body,
    })
    const toggle = wrapper.get('[role="switch"]')
    const tooltip = wrapper.get('[role="tooltip"]')

    expect(toggle.attributes('disabled')).toBeUndefined()
    expect(toggle.attributes('aria-disabled')).toBe('true')
    expect(toggle.attributes('aria-describedby')).toBe(tooltip.attributes('id'))
    toggle.element.focus()
    expect(document.activeElement).toBe(toggle.element)
    await toggle.trigger('keydown', { key: ' ' })
    expect(wrapper.emitted('update:modelValue')).toBeUndefined()
    wrapper.unmount()
  })

  it('opens and closes Select from the keyboard', async () => {
    const wrapper = mount(SettingSelect, {
      props: {
        modelValue: 'best_match',
        options: [
          { value: 'best_match', label: 'Best Match' },
          { value: 'ecmwf', label: 'ECMWF' },
          { value: 'gfs', label: 'GFS' },
        ],
      },
      attachTo: document.body,
    })
    const trigger = wrapper.get('[role="combobox"]')
    trigger.element.focus()
    await trigger.trigger('keydown', { key: 'Enter' })
    expect(document.body.querySelector('[role="listbox"]')).not.toBeNull()
    document.body.querySelector('[role="listbox"]')
      .dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await wrapper.vm.$nextTick()
    expect(trigger.attributes('aria-expanded')).toBe('false')
    wrapper.unmount()
  })

  it('renders an option separator across the dropdown', async () => {
    const wrapper = mount(SettingSelect, {
      props: {
        modelValue: 'local',
        options: [
          { value: 'local', label: 'Local model' },
          { value: 'global', label: 'Global model', separatorBefore: true },
        ],
      },
      attachTo: document.body,
    })

    await wrapper.get('[role="combobox"]').trigger('keydown', { key: 'Enter' })

    const separator = document.body.querySelector('.setting-select__separator')
    expect(separator).not.toBeNull()
    expect(separator.classList.contains('setting-select__separator')).toBe(true)
    expect(separator.nextElementSibling.textContent).toContain('Global model')
    wrapper.unmount()
  })

  it('keeps combobox search controlled and restores the committed label on dismissal', async () => {
    const wrapper = mount(SettingCombobox, {
      props: {
        modelValue: 'brouwersdam',
        searchTerm: 'Brouwersdam',
        options: spots,
        emptyText: 'No existing spots found',
        'onUpdate:searchTerm': (value) => wrapper.setProps({
          searchTerm: value,
          options: spots.filter((spot) => spot.label.toLowerCase().includes(value.toLowerCase())),
        }),
      },
      attachTo: document.body,
    })
    const input = wrapper.get('input[role="combobox"]')

    await input.setValue('Nowhere')
    expect(wrapper.emitted('search-intent')).toHaveLength(1)
    await input.setValue('')
    expect(wrapper.emitted('search-intent')).toHaveLength(2)
    await input.setValue('Nowhere')
    await input.trigger('keydown', { key: 'Escape' })

    expect(wrapper.emitted('update:modelValue')).toBeUndefined()
    expect(wrapper.emitted('update:searchTerm').at(-1)).toEqual(['Brouwersdam'])
    expect(wrapper.emitted('search-intent')).toHaveLength(3)
    expect(document.activeElement).toBe(input.element)
    expect(wrapper.find('.setting-combobox__chevron').exists()).toBe(false)
    wrapper.unmount()
  })

  it('can select the committed label when a search combobox receives focus', async () => {
    const wrapper = mount(SettingCombobox, {
      props: {
        modelValue: 'brouwersdam',
        searchTerm: 'Brouwersdam',
        options: spots,
        selectAllOnFocus: true,
      },
      attachTo: document.body,
    })
    const input = wrapper.get('input[role="combobox"]')

    input.element.focus()
    await wrapper.vm.$nextTick()

    expect(input.element.selectionStart).toBe(0)
    expect(input.element.selectionEnd).toBe('Brouwersdam'.length)
    wrapper.unmount()
  })

  it('commits a supplied combobox result and keeps focus in the input', async () => {
    const wrapper = mount(SettingCombobox, {
      props: {
        modelValue: 'brouwersdam',
        searchTerm: '',
        options: spots,
        'onUpdate:modelValue': (value) => wrapper.setProps({ modelValue: value }),
        'onUpdate:searchTerm': (value) => wrapper.setProps({
          searchTerm: value,
          options: spots.filter((spot) => spot.label.toLowerCase().includes(value.toLowerCase())),
        }),
      },
      attachTo: document.body,
    })
    const input = wrapper.get('input[role="combobox"]')
    await input.trigger('focus')
    await input.setValue('E')
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await wrapper.vm.$nextTick()

    expect(wrapper.emitted('update:modelValue')?.at(-1)).toEqual(['edam'])
    expect(wrapper.emitted('update:searchTerm')?.at(-1)).toEqual(['Edam'])
    expect(document.activeElement).toBe(input.element)
    wrapper.unmount()
  })

  it('renders compact results inline and blurs after selection or dismissal', async () => {
    const wrapper = mount(SettingCombobox, {
      props: {
        modelValue: 'brouwersdam',
        searchTerm: 'Ed',
        options: spots,
        inlineResults: true,
        blurAfterSelect: true,
        blurAfterDismiss: true,
        inputType: 'search',
        'onUpdate:modelValue': (value) => wrapper.setProps({ modelValue: value }),
        'onUpdate:searchTerm': (value) => wrapper.setProps({ searchTerm: value }),
      },
      attachTo: document.body,
    })
    const input = wrapper.get('input[role="combobox"]')
    await input.trigger('focus')

    expect(input.attributes('type')).toBe('search')
    expect(input.attributes('inputmode')).toBe('search')
    expect(wrapper.find('[role="listbox"]').exists()).toBe(true)
    expect(document.body.querySelector(':scope > [role="listbox"]')).toBeNull()

    const option = wrapper.findAll('[role="option"]')
      .find((candidate) => candidate.text().includes('Edam'))
    await option.trigger('click')
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('update:modelValue')?.at(-1)).toEqual(['edam'])
    expect(document.activeElement).not.toBe(input.element)

    await input.trigger('focus')
    await input.trigger('keydown', { key: 'Escape' })
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('dismiss')).toHaveLength(1)
    expect(document.activeElement).not.toBe(input.element)
    wrapper.unmount()
  })

  it('can keep a search field empty after selecting or dismissing a result', async () => {
    const wrapper = mount(SettingCombobox, {
      props: {
        modelValue: 'brouwersdam',
        searchTerm: '',
        options: spots,
        restoreSearchOnClose: false,
        'onUpdate:modelValue': (value) => wrapper.setProps({ modelValue: value }),
        'onUpdate:searchTerm': (value) => wrapper.setProps({ searchTerm: value }),
      },
      attachTo: document.body,
    })
    const input = wrapper.get('input[role="combobox"]')

    await input.setValue('E')
    await input.trigger('keydown', { key: 'Escape' })
    expect(wrapper.props('searchTerm')).toBe('E')

    await input.trigger('focus')
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await wrapper.vm.$nextTick()

    expect(wrapper.emitted('update:modelValue')?.at(-1)).toEqual(['edam'])
    expect(wrapper.props('searchTerm')).toBe('')
    wrapper.unmount()
  })

  it('shows the empty result message without changing the committed combobox value', async () => {
    const wrapper = mount(SettingCombobox, {
      props: {
        modelValue: 'brouwersdam',
        searchTerm: 'Nowhere',
        options: [],
        emptyText: 'No existing spots found',
      },
      attachTo: document.body,
    })
    await wrapper.get('input').trigger('focus')

    expect(document.body.textContent).toContain('No existing spots found')
    expect(wrapper.emitted('update:modelValue')).toBeUndefined()
    wrapper.unmount()
  })

  it('offers a separated create action for an unmatched search without committing it', async () => {
    const wrapper = mount(SettingCombobox, {
      props: {
        modelValue: 'brouwersdam',
        searchTerm: 'Edam harbour',
        options: [],
        emptyText: 'No existing spots found',
        createActionLabel: 'Add “Edam harbour” as a spot',
      },
      attachTo: document.body,
    })
    await wrapper.get('input').trigger('focus')

    const action = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Add “Edam harbour” as a spot'))
    expect(action).toBeDefined()
    expect(document.body.textContent).not.toContain('No existing spots found')
    expect(document.body.querySelector('.setting-popup__separator')).toBeNull()

    action.click()
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted('create')?.at(-1)).toEqual(['Edam harbour'])
    expect(wrapper.emitted('update:modelValue')).toBeUndefined()
    wrapper.unmount()
  })

  it('emits only valid number values and rolls invalid drafts back on Escape and blur', async () => {
    const onUpdate = vi.fn()
    const wrapper = mount({
      components: { SettingRow, SettingNumberInput },
      template: `
        <SettingRow label="Threshold">
          <SettingNumberInput :model-value="17" :min="5" :max="35" unit="kt" @update:model-value="onUpdate" />
        </SettingRow>
      `,
      methods: { onUpdate },
    })
    const input = wrapper.get('input[type="number"]')
    expect(input.attributes('inputmode')).toBe('numeric')
    expect(input.attributes('aria-labelledby')).toBe(wrapper.get('label').attributes('id'))

    await input.trigger('focus')
    await input.setValue('23')
    expect(onUpdate).toHaveBeenLastCalledWith(23)

    await input.setValue('40')
    expect(onUpdate).not.toHaveBeenCalledWith(40)
    expect(input.attributes('aria-invalid')).toBe('true')
    const error = wrapper.get('[role="alert"]')
    expect(error.text()).toContain('Enter a value from 5 to 35 kt')
    expect(input.attributes('aria-describedby').split(' ')).toContain(error.attributes('id'))

    await input.trigger('keydown', { key: 'Escape' })
    expect(input.element.value).toBe('17')
    expect(input.attributes('aria-invalid')).toBeUndefined()

    await input.setValue('')
    await input.trigger('blur')
    expect(input.element.value).toBe('17')
    expect(onUpdate).toHaveBeenCalledTimes(1)
  })
})
