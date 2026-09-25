import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushPromises, mount } from '@vue/test-utils'

import SpotCreationDialog from '../src/components/SpotCreationDialog.vue'

const { toastError } = vi.hoisted(() => ({ toastError: vi.fn() }))
vi.mock('vue-sonner', () => ({ toast: { error: toastError } }))
// Component tests inject the map boundary: happy-dom has no WebGL context.
// Tests that exercise map interactions supply their own createMap prop below.
vi.mock('../src/map/geoapifyMap', () => ({
  createGeoapifyMap: vi.fn(async () => ({ setCenter: vi.fn(), destroy: vi.fn() })),
}))

const edam = {
  id: 'edam-id',
  name: 'Edam',
  description: 'North Holland, Netherlands',
  latitude: 52.5126,
  longitude: 5.0486,
  timezone: 'Europe/Amsterdam',
}

function deferred() {
  let resolve
  let reject
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise
    reject = rejectPromise
  })
  return { promise, reject, resolve }
}

afterEach(() => {
  vi.useRealTimers()
  toastError.mockClear()
  document.body.innerHTML = ''
})

describe('Spot creation dialog', () => {
  it('keeps the automatic initial search visually silent', async () => {
    vi.useFakeTimers()
    const search = deferred()
    const typedSearch = deferred()
    const searchPlaces = vi.fn()
      .mockReturnValueOnce(search.promise)
      .mockReturnValueOnce(typedSearch.promise)
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces,
      },
      attachTo: document.body,
    })

    await vi.advanceTimersByTimeAsync(300)
    expect(document.body.querySelector('.spot-dialog__results')).toBeNull()

    search.resolve([edam])
    await flushPromises()
    expect(document.body.querySelector('.spot-dialog__results')).toBeNull()

    const input = document.body.querySelector('input[role="combobox"]')
    input.value = 'Edam harbour'
    input.dispatchEvent(new InputEvent('input', { bubbles: true }))
    await vi.advanceTimersByTimeAsync(300)
    expect(document.body.textContent).toContain('Searching places…')
    wrapper.unmount()
  })

  it('waits until the dialog opens before searching and debounces its initial query', async () => {
    vi.useFakeTimers()
    const searchPlaces = vi.fn().mockResolvedValue([edam])
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: false,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces,
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(500)
    expect(searchPlaces).not.toHaveBeenCalled()

    await wrapper.setProps({ open: true })
    await vi.advanceTimersByTimeAsync(299)
    expect(searchPlaces).not.toHaveBeenCalled()
    await vi.advanceTimersByTimeAsync(1)
    expect(searchPlaces).toHaveBeenCalledOnce()
    expect(searchPlaces).toHaveBeenCalledWith('Edam', expect.objectContaining({
      apiKey: 'test-key',
      bias: { latitude: 52.2, longitude: 5.3 },
      signal: expect.any(AbortSignal),
    }))
    const input = document.body.querySelector('input[role="combobox"]')
    expect(input.value).toBe('Edam')
    expect(document.body.querySelector('.spot-dialog__results')).toBeNull()
    wrapper.unmount()
  })

  it('automatically previews the best result and keeps alternatives keyboard friendly', async () => {
    vi.useFakeTimers()
    const setCenter = vi.fn()
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        createMap: vi.fn().mockResolvedValue({ destroy: vi.fn(), setCenter }),
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    await flushPromises()

    const input = document.body.querySelector('input[role="combobox"]')
    expect(input.value).toBe('Edam')
    expect(document.body.querySelector('.setting-popup')).toBeNull()
    expect(document.body.querySelector('[role="option"]')).toBeNull()
    expect(setCenter).toHaveBeenCalledWith({ latitude: 52.5126, longitude: 5.0486 }, { zoom: 13 })
    expect(document.body.querySelector('.spot-dialog__confirm').textContent).toContain('Add Edam')
    input.click()
    await wrapper.vm.$nextTick()
    expect(document.body.querySelector('[role="option"]')).not.toBeNull()
    expect(document.body.querySelector('[role="option"]').getAttribute('aria-selected')).toBe('false')
    input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))
    await flushPromises()

    expect(setCenter).toHaveBeenCalledWith({ latitude: 52.5126, longitude: 5.0486 }, { zoom: 13 })
    expect(document.activeElement).toBe(input)
    expect(document.body.querySelector('.spot-dialog__results')).toBeNull()
    wrapper.unmount()
  })

  it('replaces an unfinished query with the provider result name', async () => {
    vi.useFakeTimers()
    const setCenter = vi.fn()
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'edma',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        createMap: vi.fn().mockResolvedValue({ destroy: vi.fn(), setCenter }),
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    await flushPromises()

    const input = document.body.querySelector('input[role="combobox"]')
    expect(input.value).toBe('Edam')
    expect(document.body.querySelector('.spot-dialog__confirm').textContent).toContain('Add Edam')
    expect(setCenter).toHaveBeenCalledWith({ latitude: 52.5126, longitude: 5.0486 }, { zoom: 13 })
    expect(document.body.querySelector('.spot-dialog__results')).toBeNull()

    input.click()
    await wrapper.vm.$nextTick()
    expect(document.body.querySelector('[role="option"]')).not.toBeNull()
    expect(input.selectionStart).toBe(0)
    expect(input.selectionEnd).toBe(4)
    wrapper.unmount()
  })

  it('keeps the map in place and disables confirmation when no location is found', async () => {
    vi.useFakeTimers()
    const setCenter = vi.fn()
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Nowhere nearby',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([]),
        createMap: vi.fn().mockResolvedValue({ destroy: vi.fn(), setCenter }),
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    await flushPromises()

    expect(document.body.textContent).toContain('No location found')
    expect(document.body.querySelector('.spot-dialog__confirm').disabled).toBe(true)
    expect(setCenter).not.toHaveBeenCalled()
    wrapper.unmount()
  })

  it('turns a chosen place into a movable-map confirmation and emits the final pin', async () => {
    vi.useFakeTimers()
    const searchPlaces = vi.fn().mockResolvedValue([edam])
    const reverseLocation = vi.fn().mockResolvedValue({ timezone: 'Europe/Amsterdam' })
    const destroy = vi.fn()
    const setCenter = vi.fn((_center, _options) => {
      moveMap({ latitude: 52.50673, longitude: 5.07729 })
    })
    let moveMap
    const createMap = vi.fn(async (_element, options) => {
      moveMap = options.onCenterChange
      return { destroy, setCenter }
    })
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces,
        reverseLocation,
        createMap,
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    await wrapper.vm.$nextTick()
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await wrapper.vm.$nextTick()
    await vi.runAllTimersAsync()

    expect(createMap).toHaveBeenCalledWith(expect.any(HTMLElement), expect.objectContaining({
      center: { latitude: 52.2, longitude: 5.3 },
      apiKey: 'test-key',
      zoom: 6,
    }))
    expect(setCenter).toHaveBeenCalledWith({ latitude: 52.5126, longitude: 5.0486 }, { zoom: 13 })
    expect(document.body.textContent).toContain('Move the map until the pin is on your spot')

    const confirm = document.body.querySelector('.spot-dialog__confirm')
    confirm.click()
    await wrapper.vm.$nextTick()
    await vi.runAllTimersAsync()

    expect(reverseLocation).toHaveBeenCalledWith({
      latitude: 52.50673,
      longitude: 5.07729,
    }, expect.objectContaining({ apiKey: 'test-key' }))
    expect(wrapper.emitted('confirm')?.at(-1)).toEqual([{
      name: 'Edam',
      latitude: 52.50673,
      longitude: 5.07729,
      timezone: 'Europe/Amsterdam',
      providerRef: 'geoapify:edam-id',
    }])
    wrapper.unmount()
    expect(destroy).toHaveBeenCalledOnce()
  })

  it('does not let a late map startup position replace the searched place', async () => {
    vi.useFakeTimers()
    const pendingMap = deferred()
    const reverseLocation = vi.fn().mockResolvedValue({ timezone: 'Europe/Amsterdam' })
    let reportMapCenter
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        reverseLocation,
        createMap: vi.fn((_element, options) => {
          reportMapCenter = options.onCenterChange
          return pendingMap.promise
        }),
      },
      attachTo: document.body,
    })

    await vi.advanceTimersByTimeAsync(300)
    await flushPromises()
    reportMapCenter({ latitude: 52.2, longitude: 5.3 })
    pendingMap.resolve({ destroy: vi.fn(), setCenter: vi.fn() })
    await flushPromises()

    document.body.querySelector('.spot-dialog__confirm').click()
    await flushPromises()

    expect(reverseLocation).toHaveBeenCalledWith({
      latitude: 52.5126,
      longitude: 5.0486,
    }, expect.objectContaining({ apiKey: 'test-key' }))
    wrapper.unmount()
  })

  it('toasts provider errors and offers a retry path', async () => {
    vi.useFakeTimers()
    const searchPlaces = vi.fn().mockRejectedValue(new Error('Location search is temporarily unavailable.'))
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces,
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    await wrapper.vm.$nextTick()
    expect(toastError).toHaveBeenCalledWith(
      'Location search is temporarily unavailable.',
      { id: 'spot-search-error' },
    )
    expect(document.body.querySelector('[role="alert"]')).toBeNull()
    expect(wrapper.emitted('confirm')).toBeUndefined()
    wrapper.unmount()
  })

  it('keeps a newer search active when an older request finishes late', async () => {
    vi.useFakeTimers()
    const first = deferred()
    const second = deferred()
    const searchPlaces = vi.fn()
      .mockReturnValueOnce(first.promise)
      .mockReturnValueOnce(second.promise)
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Ed',
        apiKey: 'test-key',
        searchPlaces,
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    input.value = 'Edam'
    input.dispatchEvent(new Event('input', { bubbles: true }))
    await vi.advanceTimersByTimeAsync(300)

    first.resolve([{ ...edam, id: 'stale', name: 'Stale result' }])
    await flushPromises()
    expect(document.body.textContent).toContain('Searching places…')
    expect(document.body.textContent).not.toContain('Stale result')

    second.resolve([edam])
    await flushPromises()
    expect(input.value).toBe('Edam')
    expect(document.body.querySelector('.spot-dialog__confirm').textContent).toContain('Add Edam')
    expect(document.body.textContent).not.toContain('Stale result')
    wrapper.unmount()
  })

  it('destroys a map that finishes loading after the dialog closes', async () => {
    vi.useFakeTimers()
    const pendingMap = deferred()
    const destroy = vi.fn()
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        createMap: vi.fn().mockReturnValue(pendingMap.promise),
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    await wrapper.vm.$nextTick()
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await wrapper.vm.$nextTick()
    document.body.querySelector('.spot-dialog__close').click()

    pendingMap.resolve({ destroy })
    await flushPromises()
    expect(destroy).toHaveBeenCalledOnce()
    wrapper.unmount()
  })

  it('cancels final location lookup when the dialog closes', async () => {
    vi.useFakeTimers()
    const pendingReverse = deferred()
    const reverseLocation = vi.fn().mockReturnValue(pendingReverse.promise)
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        reverseLocation,
        createMap: vi.fn().mockResolvedValue({ destroy: vi.fn() }),
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    await wrapper.vm.$nextTick()
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await flushPromises()
    const confirm = document.body.querySelector('.spot-dialog__confirm')
    confirm.click()
    await wrapper.vm.$nextTick()
    const signal = reverseLocation.mock.calls[0][1].signal
    document.body.querySelector('.spot-dialog__close').click()
    expect(signal.aborted).toBe(true)

    pendingReverse.resolve({ timezone: 'Europe/Amsterdam' })
    await flushPromises()
    expect(wrapper.emitted('confirm')).toBeUndefined()
    wrapper.unmount()
  })

  it('saves the same pin that was used to determine the timezone', async () => {
    vi.useFakeTimers()
    const pendingReverse = deferred()
    let moveMap
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        reverseLocation: vi.fn().mockReturnValue(pendingReverse.promise),
        createMap: vi.fn(async (_element, options) => {
          moveMap = options.onCenterChange
          return { destroy: vi.fn() }
        }),
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    await wrapper.vm.$nextTick()
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await flushPromises()
    const confirm = document.body.querySelector('.spot-dialog__confirm')
    confirm.click()
    await wrapper.vm.$nextTick()
    moveMap({ latitude: 53, longitude: 6 })
    pendingReverse.resolve({ timezone: 'Europe/Amsterdam' })
    await flushPromises()

    expect(wrapper.emitted('confirm')?.at(-1)?.[0]).toMatchObject({
      latitude: 52.5126,
      longitude: 5.0486,
      timezone: 'Europe/Amsterdam',
    })
    wrapper.unmount()
  })

  it('keeps confirmation disabled when the positioning map fails', async () => {
    vi.useFakeTimers()
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        createMap: vi.fn().mockRejectedValue(new Error('The map could not be loaded.')),
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    await wrapper.vm.$nextTick()
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await flushPromises()

    const confirm = document.body.querySelector('.spot-dialog__confirm')
    expect(confirm.disabled).toBe(true)
    expect(toastError).toHaveBeenCalledWith(
      'The map could not be loaded.',
      { id: 'spot-map-error' },
    )
    wrapper.unmount()
  })

  it('stays open and announces when personal spot storage fails', async () => {
    vi.useFakeTimers()
    const saveSpot = vi.fn().mockResolvedValue(null)
    const wrapper = mount(SpotCreationDialog, {
      props: {
        open: true,
        initialQuery: 'Edam',
        apiKey: 'test-key',
        searchPlaces: vi.fn().mockResolvedValue([edam]),
        reverseLocation: vi.fn().mockResolvedValue({ timezone: 'Europe/Amsterdam' }),
        createMap: vi.fn().mockResolvedValue({ destroy: vi.fn() }),
        saveSpot,
      },
      attachTo: document.body,
    })
    await vi.advanceTimersByTimeAsync(300)
    const input = document.body.querySelector('input[role="combobox"]')
    input.dispatchEvent(new FocusEvent('focus', { bubbles: true }))
    await wrapper.vm.$nextTick()
    const option = [...document.body.querySelectorAll('[role="option"]')]
      .find((candidate) => candidate.textContent.includes('Edam'))
    option.click()
    await flushPromises()
    const confirm = document.body.querySelector('.spot-dialog__confirm')
    confirm.click()
    await flushPromises()

    expect(saveSpot).toHaveBeenCalledOnce()
    expect(wrapper.emitted('update:open')).toBeUndefined()
    expect(toastError).toHaveBeenCalledWith(
      'This spot could not be saved in this browser.',
      { id: 'spot-save-error' },
    )
    wrapper.unmount()
  })
})
