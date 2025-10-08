<template>
  <div id="app">
    <Toast />
    <ConfirmDialog />
    <AppLayout />
  </div>
</template>

<script setup>
import Toast from 'primevue/toast'
import ConfirmDialog from 'primevue/confirmdialog'
import AppLayout from './components/layout/AppLayout.vue'
import { onMounted, onUnmounted } from 'vue'
import websocket from './services/websocket.js'

// Connect to WebSocket on app mount
onMounted(() => {
  websocket.connect()
})

// Disconnect from WebSocket on app unmount
onUnmounted(() => {
  websocket.disconnect()
})
</script>

<style>
/* Remove default Vite styles */
</style>