import { createApp } from 'vue'
import { createRouter, createWebHistory } from 'vue-router'
import PrimeVue from 'primevue/config'
import Aura from '@primevue/themes/aura'
import ToastService from 'primevue/toastservice'
import ConfirmationService from 'primevue/confirmationservice'
import Tooltip from 'primevue/tooltip'

// Import global styles
import 'primeicons/primeicons.css'
import './assets/styles/main.css'

import App from './App.vue'
import router from './router.js'

const app = createApp(App)

// Configure PrimeVue with Aura Dark Theme
app.use(PrimeVue, {
  theme: {
    preset: Aura,
    options: {
      primary: 'rose',
      darkModeSelector: '.dark',
      cssLayer: {
        name: 'primevue',
        order: 'primevue'
      }
    }
  },
  ripple: true
})

// Set dark mode by default
document.documentElement.classList.add('dark')

app.use(ToastService)
app.use(ConfirmationService)
app.use(router)

// Register directives
app.directive('tooltip', Tooltip)

app.mount('#app')