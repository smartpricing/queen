<template>
  <div class="sidebar-wrapper" :class="{ collapsed: !visible }" @click.self="handleOverlayClick">
    <div class="sidebar-content">
      <nav class="sidebar-nav">
        <router-link 
          v-for="item in menuItems" 
          :key="item.path"
          :to="item.path"
          class="nav-item"
          :class="{ active: isActive(item.path) }"
        >
          <i :class="item.icon" class="nav-icon"></i>
          <span class="nav-label">{{ item.label }}</span>
          <Badge 
            v-if="item.badge" 
            :value="item.badge" 
            :severity="item.badgeSeverity"
            class="nav-badge"
          />
        </router-link>
      </nav>
      
      <div class="sidebar-footer">
        <div class="system-stats">
          <div class="stat-item">
            <span class="stat-label">Uptime</span>
            <span class="stat-value">{{ uptime }}</span>
          </div>
          <div class="stat-item">
            <span class="stat-label">Version</span>
            <span class="stat-value">v2.0.0</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRoute } from 'vue-router'
import Badge from 'primevue/badge'
import api from '../../services/api.js'
import { formatRelativeTime } from '../../utils/helpers.js'

const props = defineProps({
  visible: {
    type: Boolean,
    default: true
  }
})

const emit = defineEmits(['update:visible'])
const route = useRoute()

const visible = computed({
  get: () => props.visible,
  set: (val) => emit('update:visible', val)
})

// Menu items
const menuItems = ref([
  {
    path: '/',
    label: 'Dashboard',
    icon: 'pi pi-home'
  },
  {
    path: '/queues',
    label: 'Queues',
    icon: 'pi pi-inbox',
    badge: null,
    badgeSeverity: 'info'
  },
  {
    path: '/analytics',
    label: 'Analytics',
    icon: 'pi pi-chart-line'
  },
  {
    path: '/messages',
    label: 'Messages',
    icon: 'pi pi-envelope'
  },
  {
    path: '/system',
    label: 'System',
    icon: 'pi pi-server'
  }
])

// System stats
const uptime = ref('--')
const startTime = ref(null)

// Check if route is active
const isActive = (path) => {
  if (path === '/') {
    return route.path === '/'
  }
  return route.path.startsWith(path)
}

// Fetch system health
const fetchHealth = async () => {
  try {
    const data = await api.getHealth()
    if (data.uptime) {
      // Parse uptime (format: "3600s")
      const seconds = parseInt(data.uptime)
      const hours = Math.floor(seconds / 3600)
      const minutes = Math.floor((seconds % 3600) / 60)
      
      if (hours > 24) {
        const days = Math.floor(hours / 24)
        uptime.value = `${days}d ${hours % 24}h`
      } else if (hours > 0) {
        uptime.value = `${hours}h ${minutes}m`
      } else {
        uptime.value = `${minutes}m`
      }
    }
  } catch (error) {
    console.error('Failed to fetch health:', error)
  }
}

// Handle overlay click on mobile
const handleOverlayClick = () => {
  if (window.innerWidth <= 768) {
    visible.value = false
  }
}

// Fetch queue count for badge
const fetchQueueCount = async () => {
  try {
    const data = await api.getQueues()
    if (data.queues) {
      const queueItem = menuItems.value.find(item => item.path === '/queues')
      if (queueItem) {
        queueItem.badge = data.queues.length
      }
    }
  } catch (error) {
    console.error('Failed to fetch queue count:', error)
  }
}

let healthInterval = null

onMounted(() => {
  fetchHealth()
  fetchQueueCount()
  
  // Refresh health every 30 seconds
  healthInterval = setInterval(() => {
    fetchHealth()
  }, 30000)
})

onUnmounted(() => {
  if (healthInterval) {
    clearInterval(healthInterval)
  }
})
</script>

<style scoped>
.sidebar-wrapper {
  position: fixed;
  left: 0;
  top: 60px;
  width: 250px;
  height: calc(100vh - 60px);
  background: white;
  box-shadow: 2px 0 12px rgba(0, 0, 0, 0.08);
  transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  z-index: 99;
  overflow: hidden;
}

.sidebar-wrapper.collapsed {
  transform: translateX(-100%);
}

.sidebar-content {
  height: 100%;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  background: white;
}

.sidebar-nav {
  padding: 1rem 0;
  flex: 1;
  overflow-y: auto;
}

.nav-item {
  display: flex;
  align-items: center;
  padding: 0.75rem 1.5rem;
  color: var(--gray-700);
  text-decoration: none;
  transition: all 0.2s ease;
  position: relative;
}

.nav-item:hover {
  background: var(--gray-50);
  color: var(--primary-color);
}

.nav-item.active {
  background: linear-gradient(90deg, rgba(59, 130, 246, 0.1) 0%, transparent 100%);
  color: var(--primary-color);
}

.nav-item.active::before {
  content: '';
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: 3px;
  background: var(--primary-color);
}

.nav-icon {
  font-size: 1.125rem;
  margin-right: 0.75rem;
  width: 1.5rem;
}

.nav-label {
  flex: 1;
  font-weight: 500;
  font-size: 0.875rem;
}

.nav-badge {
  margin-left: auto;
}

.sidebar-footer {
  padding: 1rem 1.5rem;
  border-top: 1px solid var(--gray-200);
  background: var(--gray-50);
}

.system-stats {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.stat-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.stat-label {
  font-size: 0.875rem;
  color: var(--gray-600);
}

.stat-value {
  font-size: 0.875rem;
  font-weight: 600;
  color: var(--gray-800);
}

/* Mobile responsive */
@media (max-width: 768px) {
  .sidebar-wrapper {
    width: 280px;
    box-shadow: 4px 0 24px rgba(0, 0, 0, 0.15);
  }
  
  .sidebar-wrapper:not(.collapsed)::before {
    content: '';
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background: rgba(0, 0, 0, 0.5);
    z-index: -1;
    animation: fadeIn 0.3s ease;
  }
  
  .nav-label {
    font-size: 0.9rem;
  }
  
  .sidebar-footer {
    padding: 0.75rem 1.25rem;
  }
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}
</style>
