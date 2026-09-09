<script setup>
import { useConfiguratorStore } from '../../stores/configurator'
import SettingRow from './SettingRow.vue'
import SettingSelect from './SettingSelect.vue'
import SettingSwitch from './SettingSwitch.vue'

const store = useConfiguratorStore()
const labels = { wind: 'Wind', swell: 'Waves', weather: 'Weather', temperature: 'Temperature', tide: 'Tide' }
const sizes = [{ value: 'off', label: 'Hide' }, { value: 'small', label: 'Numbers' }, { value: 'large', label: 'Graph' }]
</script>

<template>
  <div class="forecast-modules">
    <div v-for="id in store.moduleOrder" :key="id" class="forecast-module">
      <SettingRow :label="labels[id]">
        <SettingSelect v-if="id === 'wind' || id === 'swell'" :model-value="store[`${id}Size`]"
          :options="sizes" :name="`${id}-size`" @update:model-value="store.setModuleSize(id, $event)" />
        <SettingSwitch v-else-if="id === 'weather'" :model-value="store.showWeather" name="show-weather"
          @update:model-value="store.setShowWeather" />
        <SettingSwitch v-else-if="id === 'temperature'" :model-value="store.showTemperature" name="show-temperature"
          @update:model-value="store.setShowTemperature" />
        <SettingSwitch v-else :model-value="store.effectiveShowTide" name="show-tide"
          :disabled="!store.tideAvailable" :disabled-reason="store.tideAvailable ? '' : store.tideMessage"
          @update:model-value="store.setShowTide" />
      </SettingRow>
    </div>
    <p v-if="store.swellSize !== 'off' && ['failed', 'unavailable'].includes(store.swellStatus)"
      class="setting-row__error" role="status">{{ store.swellStatus === 'failed' ? 'Could not load waves.' : 'No wave data here.' }}</p>
  </div>
</template>

<style scoped>
.forecast-modules { display: grid; gap: 8px; }
.forecast-module { display: grid; gap: 4px; }
</style>
