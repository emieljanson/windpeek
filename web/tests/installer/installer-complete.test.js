import { afterEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import InstallerComplete from '../../src/components/installer/InstallerComplete.vue'

afterEach(() => vi.restoreAllMocks())

describe('installer completion step', () => {
  it('uses the live Windpeek donation link by default', async () => {
    const open = vi.spyOn(window, 'open').mockImplementation(() => null)
    const wrapper = mount(InstallerComplete)

    await wrapper.get('.installer-primary').trigger('click')

    expect(open).toHaveBeenCalledWith(
      'https://donate.stripe.com/6oU14o3Hy1Xg5C02291wY00',
      '_blank',
      'noopener,noreferrer',
    )
  })

  it('offers a donation before finishing when a payment link is configured', async () => {
    const open = vi.spyOn(window, 'open').mockImplementation(() => null)
    const wrapper = mount(InstallerComplete, {
      props: { donationUrl: 'https://donate.stripe.com/test-link' },
    })

    expect(wrapper.get('h2').text()).toBe('Ready for the wind')
    expect(wrapper.get('.installer-step__copy p').text()).toBe(
      'Windpeek is free, so you can decide what it’s worth to you. If it’s useful, a donation can help make future features possible.',
    )

    const actions = wrapper.findAll('.installer-primary')
    expect(actions.map((action) => action.text())).toEqual(['Donate', 'Done'])
    expect(actions[0].attributes('data-autofocus')).toBeUndefined()
    expect(actions[1].attributes('data-autofocus')).toBeDefined()

    await actions[0].trigger('click')
    expect(open).toHaveBeenCalledWith(
      'https://donate.stripe.com/test-link',
      '_blank',
      'noopener,noreferrer',
    )

    await actions[1].trigger('click')
    expect(wrapper.emitted('done')).toHaveLength(1)
  })

  it.each(['', 'http://payments.example.test'])(
    'keeps a working completion action when the payment link is unavailable or unsafe',
    (donationUrl) => {
      const wrapper = mount(InstallerComplete, { props: { donationUrl } })

      expect(wrapper.get('h2').text()).toBe('Ready for the wind')
      expect(wrapper.get('.installer-step__copy p').text()).toBe(
        'The selected spot and display options are live on your device.',
      )
      expect(wrapper.findAll('.installer-primary').map((action) => action.text())).toEqual(['Done'])
    },
  )
})
