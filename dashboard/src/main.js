import { createApp } from 'vue';
import router from './router';
import App from './App.vue';
import './assets/main.css';
import './config/theme'; // Apply theme CSS variables

const app = createApp(App);

app.use(router);

app.mount('#app');
