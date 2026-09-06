import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import LandingView from '../src/views/LandingView.vue'

describe('Windscout landing page', () => {
  it('presents every compatible device equally before the configuration step', () => {
    const wrapper = mount(LandingView, {
      global: {
        stubs: {
          LandingHero: { template: '<figure data-testid="hero"></figure>' },
        },
      },
    })

    expect(wrapper.get('h1').text()).toBe('The always-on wind forecast for your favorite spot')
    expect(wrapper.get('.intro').text()).toContain('e-ink display')
    expect(wrapper.get('.story').text()).toContain('later discover it turned into a great session')
    expect(wrapper.findAll('.facts li')).toHaveLength(5)
    expect(wrapper.get('.facts').text()).toContain('16 forecast models')
    expect(wrapper.get('.facts').text()).toContain('worldwide ECMWF, ICON and GFS')
    expect(wrapper.get('.facts').text()).toContain('13 local models')
    expect(wrapper.get('.facts').text()).not.toContain('choose units')
    expect(wrapper.get('.faq').text()).not.toContain('Best fit')
    expect(wrapper.findAll('h2').map(heading => heading.text())).toEqual([
      'Choose your reTerminal',
      'Configure & install',
      'Questions before you start',
    ])
    expect(wrapper.get('.purchase').text()).toContain('free software')
    expect(wrapper.get('.purchase').text()).toContain('~$74')
    expect(wrapper.get('.personalize').text()).toContain('Months between charges')
    expect(wrapper.findAll('.faq details')).toHaveLength(7)
    const devices = wrapper.findAll('.hardware-model')
    expect(devices).toHaveLength(3)
    expect(devices.map(device => device.get('.hardware-model__name').text())).toEqual(['E1001', 'E1002', 'E1003'])
    const specs = wrapper.findAll('.hardware-spec')
    expect(specs.map(spec => spec.get('dt').text())).toEqual(['Screen', 'Threshold line', 'Battery'])
    expect(specs.map(spec => spec.findAll('dd').map(value => value.findAll('.hardware-spec__line').map(line => line.text())))).toEqual([
      [['7.5″ · 4 greys', '800 × 480'], ['7.3″ · 6 colours', '800 × 480'], ['10.3″ · 16 greys', '1872 × 1404']],
      [['Black'], ['Red'], ['Black']],
      [['Up to 3 months'], ['Up to 3 months'], ['Up to 6 months']],
    ])
    devices.forEach((device, index) => {
      const model = `E100${index + 1}`
      const image = device.get('img')

      expect(image.attributes('src')).toContain(`devices/previews/e100${index + 1}.png`)
      expect(image.attributes('loading')).toBe('lazy')
      expect(image.attributes('decoding')).toBe('async')
      expect(device.find('.hardware-model__buy').exists()).toBe(false)
    })
    const buyLinks = wrapper.findAll('.hardware-model__buy')
    expect(buyLinks).toHaveLength(3)
    buyLinks.forEach((buyLink, index) => {
      const model = `E100${index + 1}`
      expect(buyLink.attributes('href')).toContain(`seeedstudio.com/reTerminal-${model}`)
      expect(buyLink.attributes('href')).toContain('sensecap_affiliate=UF4PmgK')
      expect(buyLink.attributes('href')).toContain('referring_service=link')
      expect(buyLink.attributes('target')).toBe('_blank')
      expect(buyLink.attributes('rel')).toContain('noopener')
      expect(buyLink.text()).toBe(`Buy for ${['~$74', '~$107', '~$160'][index]}`)
    })
    expect(wrapper.find('.hardware-compare').exists()).toBe(false)
    expect(wrapper.get('.configure-action--desktop').attributes('href')).toBe('?configure')
    expect(wrapper.get('.configure-action--desktop').text()).toBe('Configure & install')
    expect(wrapper.get('.configure-action--mobile').attributes('disabled')).toBeDefined()
    expect(wrapper.get('.configure-action--mobile').text()).toBe('Configure & install on desktop')
    expect(wrapper.find('.configure-desktop-note').exists()).toBe(false)
    expect(wrapper.find('.quiet-note').exists()).toBe(false)
    const donationLink = wrapper.get('.faq a')
    expect(donationLink.attributes('href')).toBe('https://donate.stripe.com/6oU14o3Hy1Xg5C02291wY00')
    expect(donationLink.attributes('target')).toBe('_blank')
    expect(donationLink.attributes('rel')).toContain('noopener')
    expect(wrapper.find('[role="dialog"]').exists()).toBe(false)
    expect(wrapper.get('[data-testid="hero"]').exists()).toBe(true)
  })
})
