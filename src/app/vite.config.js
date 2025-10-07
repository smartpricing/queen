import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [vue()],
  server: {
    port: 5160,
    proxy: {
      '/api': {
        target: 'http://localhost:6632',
        changeOrigin: true
      },
      '/ws': {
        target: 'ws://localhost:6632',
        ws: true,
        changeOrigin: true
      }
    }
  },
  define: {
    'process.env': {}
  }
})