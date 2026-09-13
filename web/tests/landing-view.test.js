import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import LandingView from '../src/views/LandingView.vue'

describe('Windpeek landing page', () => {
  it('uses swell renders for all three devices on the swell landing', () => {
    window.history.replaceState({}, '', '/?site=swell')
    const wrapper = mount(LandingView, { global: { stubs: { LandingHero: true } } })
    expect(wrapper.findAll('.hardware-model img').map(image => image.attributes('src'))).toEqual([
      '/devices/previews/e1003-swell.png', '/devices/previews/e1002-swell.png', '/devices/previews/e1001-swell.png',
    ])
    wrapper.unmount()
    window.history.replaceState({}, '', '/')
  })

  it('recommends E1003 and presents every compatible device before the configuration step', () => {
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
    expect(wrapper.findAll('.facts li')).toHaveLength(6)
    expect(wrapper.get('.facts').text()).toContain('Global & regional models')
    expect(wrapper.get('.facts').text()).toContain('wind and wave forecasts')
    expect(wrapper.get('.facts').text()).toContain('Your screen, your choice')
    expect(wrapper.get('.facts').text()).not.toContain('choose units')
    expect(wrapper.get('.faq').text()).not.toContain('Best fit')
    expect(wrapper.findAll('h2').map(heading => heading.text())).toEqual([
      'Choose your reTerminal',
      'Configure & install',
      'Questions before you start',
    ])
    expect(wrapper.get('.purchase').text()).toContain('free software')
    expect(wrapper.get('.purchase').text()).toContain('E1003 stores up to 10 spots')
    expect(wrapper.get('.purchase').text()).toContain('compare three at a time')
    expect(wrapper.get('.purchase').text()).toContain('~$74')
    expect(wrapper.get('.personalize').text()).toContain('Months between charges')
    expect(wrapper.findAll('.faq details')).toHaveLength(6)
    const devices = wrapper.findAll('.hardware-model')
    expect(devices).toHaveLength(3)
    expect(devices.map(device => device.get('.hardware-model__name').text())).toEqual(['E1003Our pick', 'E1002', 'E1001'])
    const specs = wrapper.findAll('.hardware-spec')
    expect(specs.map(spec => spec.get('dt').text())).toEqual(['Screen', 'Screen resolution', 'Saved spots', 'Forecast view', 'Hourly detail', 'Touchscreen', 'Battery'])
    wrapper.findAll('.hardware-spec__copy--mobile').forEach(copy => {
      expect(copy.attributes('aria-hidden')).toBeUndefined()
    })
    expect(specs.map(spec => spec.findAll('dd').map(value => value.findAll('.hardware-spec__line').map(line => line.find('.hardware-spec__copy--desktop').exists() ? line.get('.hardware-spec__copy--desktop').text() : line.text())))).toEqual([
      [['10.3″, 16 greys'], ['7.3″, 6 colours'], ['7.5″, 4 greys']],
      [['High-res screen'], ['Standard screen'], ['Standard screen']],
      [['Up to 10 spots'], ['1 spot'], ['1 spot']],
      [['3-spot overview'], ['—'], ['—']],
      [['Hourly detail'], ['—'], ['—']],
      [['Touchscreen'], ['—'], ['—']],
      [['6 month battery'], ['3 month battery'], ['3 month battery']],
    ])
    expect(wrapper.get('.hardware-spec--spots').text()).toContain('Up to 10 spots')
    expect(wrapper.get('.hardware-spec--controls').text()).not.toContain('Buttons')
    devices.forEach((device, index) => {
      const model = `E100${3 - index}`
      const image = device.get('img')

      expect(image.attributes('src')).toContain(`devices/previews/e100${3 - index}-wind.png`)
      expect(image.attributes('loading')).toBe('lazy')
      expect(image.attributes('decoding')).toBe('async')
      expect(device.find('.hardware-model__buy').exists()).toBe(false)
    })
    const buyLinks = wrapper.findAll('.hardware-model__buy')
    expect(buyLinks).toHaveLength(3)
    buyLinks.forEach((buyLink, index) => {
      const model = `E100${3 - index}`
      expect(buyLink.element.tagName).toBe('BUTTON')
      expect(buyLink.attributes('aria-haspopup')).toBe('dialog')
      expect(buyLink.attributes('aria-label')).toContain(model)
      expect(buyLink.text()).toBe(`Buy for ${['~$157', '~$107', '~$74'][index]}`)
    })
    expect(wrapper.find('.hardware-compare').exists()).toBe(false)
    expect(wrapper.get('.configure-action--desktop').attributes('href')).toContain('?configure=')
    expect(wrapper.get('.configure-action--desktop').text()).toBe('Configure & install')
    expect(wrapper.get('.configure-action--mobile').attributes('disabled')).toBeDefined()
    expect(wrapper.get('.configure-action--mobile').text()).toBe('Configure & install on desktop')
    expect(wrapper.find('.configure-desktop-note').exists()).toBe(false)
    expect(wrapper.find('.quiet-note').exists()).toBe(false)
    const forecastQuestion = wrapper.findAll('.faq details').find(item => item.get('summary').text() === 'Which forecast models can I use?')
    expect(forecastQuestion.text()).toContain('Forecast data comes from Open-Meteo')
    expect(forecastQuestion.get('a').attributes('href')).toBe('https://open-meteo.com/')
    const donationLink = wrapper.get('.faq a[href^="https://donate.stripe.com/"]')
    expect(donationLink.attributes('href')).toBe('https://donate.stripe.com/6oU14o3Hy1Xg5C02291wY00')
    expect(donationLink.attributes('target')).toBe('_blank')
    expect(donationLink.attributes('rel')).toContain('noopener')
    expect(wrapper.find('[role="dialog"]').exists()).toBe(false)
    expect(wrapper.get('[data-testid="hero"]').exists()).toBe(true)
  })
})
