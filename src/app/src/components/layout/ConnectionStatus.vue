<template>
  <div class="connection-status" :class="{ collapsed }">
    <div class="status-indicator" :class="statusClass"></div>
    <span v-if="!collapsed" class="status-text">{{ statusText }}</span>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { useWebSocket } from '../../composables/useWebSocket'

defineProps({
  collapsed: Boolean
})

const { isConnected, reconnecting } = useWebSocket()

const statusClass = computed(() => {
  if (isConnected.value) return 'connected'
  if (reconnecting.value) return 'reconnecting'
  return 'disconnected'
})

const statusText = computed(() => {
  if (isConnected.value) return 'Connected'
  if (reconnecting.value) return 'Reconnecting...'
  return 'Disconnected'
})
</script>

<style scoped>
.connection-status {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem;
  background: var(--surface-200);
  border-radius: 0.5rem;
}

.connection-status.collapsed {
  justify-content: center;
}

.status-indicator {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  transition: all 0.3s;
}

.status-indicator.connected {
  background: #10b981;
  box-shadow: 0 0 8px rgba(16, 185, 129, 0.5);
}

.status-indicator.reconnecting {
  background: #f59e0b;
  animation: pulse 1s infinite;
}

.status-indicator.disconnected {
  background: #ef4444;
}

.status-text {
  font-size: 0.875rem;
  color: #a3a3a3;
}
</style>
