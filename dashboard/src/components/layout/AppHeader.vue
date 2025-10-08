<template>
  <header class="app-header">
    <div class="header-content">
      <div class="header-left">
        <Button 
          icon="pi pi-bars" 
          class="p-button-text p-button-plain menu-toggle"
          @click="toggleSidebar"
        />
        <div class="logo">
          <i class="pi pi-crown logo-icon"></i>
          <span class="logo-text">Queen Dashboard</span>
        </div>
      </div>
      
      <div class="header-center">
        <h1 class="page-title">{{ pageTitle }}</h1>
      </div>
      
      <div class="header-right">
        <div class="connection-status" :class="connectionClass">
          <span class="indicator"></span>
          <span>{{ connectionText }}</span>
        </div>
        
        <Button 
          icon="pi pi-refresh" 
          class="p-button-text p-button-plain header-btn"
          v-tooltip="'Refresh'"
          @click="handleRefresh"
        />
        
        <Button 
          icon="pi pi-cog" 
          class="p-button-text p-button-plain header-btn"
          v-tooltip="'Settings'"
        />
      </div>
    </div>
  </header>
</template>

<script setup>
import { ref, computed, watch, inject } from 'vue'
import { useRoute } from 'vue-router'
import Button from 'primevue/button'
import websocket from '../../services/websocket.js'

const route = useRoute()
const toggleSidebar = inject('toggleSidebar')

// Connection status
const isConnected = ref(websocket.getConnectionStatus())

// Watch for connection changes
websocket.on('connected', () => {
  isConnected.value = true
})

websocket.on('disconnected', () => {
  isConnected.value = false
})

const connectionClass = computed(() => ({
  connected: isConnected.value,
  disconnected: !isConnected.value
}))

const connectionText = computed(() => 
  isConnected.value ? 'Connected' : 'Disconnected'
)

// Page title from route
const pageTitle = computed(() => route.meta.title || 'Dashboard')

// Refresh handler
const handleRefresh = () => {
  window.location.reload()
}
</script>

<style scoped>
.app-header {
  height: 64px;
  background: rgba(28, 25, 23, 0.95);  /* stone-900 with opacity */
  backdrop-filter: blur(12px);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.3);
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
  position: sticky;
  top: 0;
  z-index: 100;
}

.header-content {
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 1.5rem;
}

.header-left,
.header-right {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.menu-toggle {
  display: flex !important;
  margin-right: 1rem;
  color: var(--surface-600) !important;
}

.menu-toggle:hover {
  color: var(--primary-500) !important;
  background: rgba(236, 72, 153, 0.1) !important;
}

.logo {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 1rem;
  font-weight: 600;
  background: var(--gradient-primary);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.logo-icon {
  font-size: 1.25rem;
  color: var(--primary-500);
}

.page-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0;
}

.connection-status {
  display: inline-flex;
  align-items: center;
  gap: 0.25rem;
  padding: 0.25rem 0.75rem;
  border-radius: 9999px;
  font-size: 0.875rem;
  font-weight: 500;
  transition: all 0.3s ease;
}

.connection-status.connected {
  background: rgba(16, 185, 129, 0.15);
  color: var(--success-color);
}

.connection-status.disconnected {
  background: rgba(239, 68, 68, 0.15);
  color: var(--danger-color);
}

.connection-status .indicator {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: currentColor;
}

.connection-status.connected .indicator {
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.5;
  }
}

.header-btn {
  color: var(--surface-500) !important;
}

.header-btn:hover {
  color: var(--primary-500) !important;
  background: rgba(236, 72, 153, 0.1) !important;
}

/* Responsive */
@media (max-width: 768px) {
  .menu-toggle {
    display: flex;
  }
  
  .header-center {
    display: none;
  }
  
  .logo-text {
    display: none;
  }
}
</style>
