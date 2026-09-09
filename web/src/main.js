import { createApp } from 'vue'
import { createPinia } from 'pinia'
import '@fontsource-variable/inter'
import './styles/base.css'
import App from './App.vue'
import { initializeConfigurator } from './config/initializeConfigurator'

createApp(App).use(createPinia().use(initializeConfigurator)).mount('#app')
