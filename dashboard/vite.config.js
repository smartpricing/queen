import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// https://vite.dev/config/
export default defineConfig({
  plugins: [vue()],
  server: {
    port: 4000,
    proxy: {
      '/api': {
        target: 'http://localhost:6632',
        changeOrigin: true
      },
      '/health': {
        target: 'http://localhost:6632',
        changeOrigin: true
      },
      '/metrics': {
        target: 'http://localhost:6632',
        changeOrigin: true
      }
    }
  },
  base: '/dashboard/',
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    sourcemap: false
  }
})