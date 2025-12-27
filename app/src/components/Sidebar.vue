<template>
  <!-- Mobile overlay -->
  <div 
    v-if="isOpen"
    class="fixed inset-0 bg-dark-500/50 backdrop-blur-sm z-40 lg:hidden"
    @click="close"
  />
  
  <!-- Mobile toggle button -->
  <button
    @click="toggle"
    class="fixed top-4 left-4 z-50 lg:hidden p-2 rounded-lg bg-white dark:bg-dark-200 shadow-lg border border-light-200 dark:border-dark-50"
  >
    <svg v-if="!isOpen" class="w-6 h-6 text-light-700 dark:text-light-300" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
      <path stroke-linecap="round" stroke-linejoin="round" d="M3.75 6.75h16.5M3.75 12h16.5m-16.5 5.25h16.5" />
    </svg>
    <svg v-else class="w-6 h-6 text-light-700 dark:text-light-300" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
      <path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
    </svg>
  </button>
  
  <aside 
    class="fixed left-0 top-0 bottom-0 w-64 flex flex-col z-40 transition-transform duration-300 lg:translate-x-0"
    :class="isOpen ? 'translate-x-0' : '-translate-x-full'"
  >
    <!-- Background -->
    <div class="absolute inset-0 bg-white/80 dark:bg-dark-300/90 backdrop-blur-xl border-r border-light-200/50 dark:border-dark-50/50" />
    
    <!-- Content -->
    <div class="relative flex flex-col h-full">
      <!-- Logo -->
      <div class="px-6 py-5 flex items-center gap-3">
        <div class="relative">
          <img 
            src="/queen-logo.png" 
            alt="Queen" 
            class="w-10 h-10 object-contain"
          />
          <!-- Status indicator -->
          <span 
            class="absolute -bottom-0.5 -right-0.5 w-3 h-3 rounded-full border-2 border-white dark:border-dark-300"
            :class="isConnected ? 'bg-emerald-500' : 'bg-rose-500'"
          />
        </div>
        <div>
          <h1 class="font-display font-bold text-lg text-light-900 dark:text-white">
            Queen
          </h1>
          <p class="text-xs text-light-500 dark:text-light-500">
            Message Queue
          </p>
        </div>
      </div>
      
      <!-- Navigation -->
      <nav class="flex-1 px-3 py-4 space-y-1 overflow-y-auto scrollbar-hide">
        <router-link
          v-for="item in navigation"
          :key="item.name"
          :to="item.path"
          class="nav-item group"
          :class="{ 'nav-item-active': isActive(item.path) }"
        >
          <component 
            :is="item.icon" 
            class="w-5 h-5 transition-colors"
            :class="isActive(item.path) ? 'text-queen-500' : 'text-light-500 dark:text-light-500 group-hover:text-queen-500'"
          />
          <span>{{ item.name }}</span>
          
          <!-- Badge for notifications -->
          <span 
            v-if="item.badge" 
            class="ml-auto badge"
            :class="item.badgeClass || 'badge-queen'"
          >
            {{ item.badge }}
          </span>
        </router-link>
      </nav>
      
      <!-- Footer -->
      <div class="px-4 py-4 border-t border-light-200/50 dark:border-dark-50/50">
        <!-- Health status -->
        <div class="flex items-center gap-3 px-3 py-2 rounded-lg bg-light-100 dark:bg-dark-200">
          <div class="flex items-center gap-2">
            <span 
              class="status-dot"
              :class="healthStatus.class"
            />
            <span class="text-sm font-medium text-light-700 dark:text-light-300">
              {{ healthStatus.text }}
            </span>
          </div>
          <button 
            @click="refreshHealth"
            class="ml-auto p-1.5 rounded-md hover:bg-light-200 dark:hover:bg-dark-100 transition-colors"
            :class="{ 'animate-spin': loadingHealth }"
          >
            <RefreshIcon class="w-4 h-4 text-light-500" />
          </button>
        </div>
        
        <!-- User info & Logout (only when behind proxy) -->
        <div v-if="isProxied" class="mt-3 flex items-center justify-between px-3 py-2 rounded-lg bg-light-100 dark:bg-dark-200">
          <div class="flex items-center gap-2 min-w-0">
            <div class="w-6 h-6 rounded-full bg-queen-500/20 flex items-center justify-center flex-shrink-0">
              <span class="text-xs font-semibold text-queen-600 dark:text-queen-400">
                {{ proxyUser?.username?.charAt(0)?.toUpperCase() || 'U' }}
              </span>
            </div>
            <span class="text-xs text-light-600 dark:text-light-400 truncate">
              {{ proxyUser?.username || 'User' }}
            </span>
          </div>
          <button 
            @click="logout"
            class="p-1.5 rounded-md text-light-500 hover:text-rose-500 hover:bg-rose-50 dark:hover:bg-rose-900/20 transition-colors"
            title="Logout"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.5">
              <path stroke-linecap="round" stroke-linejoin="round" d="M15.75 9V5.25A2.25 2.25 0 0013.5 3h-6a2.25 2.25 0 00-2.25 2.25v13.5A2.25 2.25 0 007.5 21h6a2.25 2.25 0 002.25-2.25V15M12 9l-3 3m0 0l3 3m-3-3h12.75" />
            </svg>
          </button>
        </div>
        
        <!-- Version info -->
        <p class="text-xs text-light-500 text-center mt-3">
          v{{ appVersion }}
        </p>
      </div>
    </div>
  </aside>
</template>

<script setup>
import { ref, computed, onMounted, watch, h } from 'vue'
import { useRoute } from 'vue-router'
import { system } from '@/api'
import { useProxy } from '@/composables/useProxy'
import { version as appVersion } from '../../package.json'

const route = useRoute()
const { isProxied, proxyUser, logout } = useProxy()

// Mobile sidebar state
const isOpen = ref(false)

const toggle = () => {
  isOpen.value = !isOpen.value
}

const close = () => {
  isOpen.value = false
}

// Close sidebar on route change (mobile)
watch(() => route.path, () => {
  if (window.innerWidth < 1024) {
    close()
  }
})

// Health check
const health = ref(null)
const loadingHealth = ref(false)
const isConnected = computed(() => 
  health.value?.status === 'healthy' || health.value?.status === 'ok'
)

const healthStatus = computed(() => {
  if (loadingHealth.value) {
    return { text: 'Checking...', class: 'bg-light-400' }
  }
  if (!health.value) {
    return { text: 'Disconnected', class: 'status-dot-danger' }
  }
  if (health.value.status === 'healthy' || health.value.status === 'ok') {
    return { text: 'Healthy', class: 'status-dot-success' }
  }
  return { text: 'Degraded', class: 'status-dot-warning' }
})

const refreshHealth = async () => {
  loadingHealth.value = true
  try {
    const response = await system.getHealth()
    health.value = response.data
  } catch (err) {
    health.value = null
  } finally {
    loadingHealth.value = false
  }
}

onMounted(() => {
  refreshHealth()
  // Auto-refresh health every 30s
  setInterval(refreshHealth, 30000)
})

// Navigation
const navigation = ref([
  { name: 'Dashboard', path: '/', icon: DashboardIcon },
  { name: 'Queues', path: '/queues', icon: QueuesIcon },
  { name: 'Consumers', path: '/consumers', icon: ConsumersIcon },
  { name: 'Messages', path: '/messages', icon: MessagesIcon },
  { name: 'Traces', path: '/traces', icon: TracesIcon },
  { name: 'Analytics', path: '/analytics', icon: AnalyticsIcon },
  { name: 'System', path: '/system', icon: SystemIcon },
])

const isActive = (path) => {
  if (path === '/') {
    return route.path === '/'
  }
  return route.path.startsWith(path)
}

// Icons as render functions
function DashboardIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M3.75 6A2.25 2.25 0 016 3.75h2.25A2.25 2.25 0 0110.5 6v2.25a2.25 2.25 0 01-2.25 2.25H6a2.25 2.25 0 01-2.25-2.25V6zM3.75 15.75A2.25 2.25 0 016 13.5h2.25a2.25 2.25 0 012.25 2.25V18a2.25 2.25 0 01-2.25 2.25H6A2.25 2.25 0 013.75 18v-2.25zM13.5 6a2.25 2.25 0 012.25-2.25H18A2.25 2.25 0 0120.25 6v2.25A2.25 2.25 0 0118 10.5h-2.25a2.25 2.25 0 01-2.25-2.25V6zM13.5 15.75a2.25 2.25 0 012.25-2.25H18a2.25 2.25 0 012.25 2.25V18A2.25 2.25 0 0118 20.25h-2.25A2.25 2.25 0 0113.5 18v-2.25z' })
  ])
}

function QueuesIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M3.75 12h16.5m-16.5 3.75h16.5M3.75 19.5h16.5M5.625 4.5h12.75a1.875 1.875 0 010 3.75H5.625a1.875 1.875 0 010-3.75z' })
  ])
}

function MessagesIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M21.75 6.75v10.5a2.25 2.25 0 01-2.25 2.25h-15a2.25 2.25 0 01-2.25-2.25V6.75m19.5 0A2.25 2.25 0 0019.5 4.5h-15a2.25 2.25 0 00-2.25 2.25m19.5 0v.243a2.25 2.25 0 01-1.07 1.916l-7.5 4.615a2.25 2.25 0 01-2.36 0L3.32 8.91a2.25 2.25 0 01-1.07-1.916V6.75' })
  ])
}

function TracesIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M7.5 21L3 16.5m0 0L7.5 12M3 16.5h13.5m0-13.5L21 7.5m0 0L16.5 12M21 7.5H7.5' })
  ])
}

function ConsumersIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M18 18.72a9.094 9.094 0 003.741-.479 3 3 0 00-4.682-2.72m.94 3.198l.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0112 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 016 18.719m12 0a5.971 5.971 0 00-.941-3.197m0 0A5.995 5.995 0 0012 12.75a5.995 5.995 0 00-5.058 2.772m0 0a3 3 0 00-4.681 2.72 8.986 8.986 0 003.74.477m.94-3.197a5.971 5.971 0 00-.94 3.197M15 6.75a3 3 0 11-6 0 3 3 0 016 0zm6 3a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0zm-13.5 0a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0z' })
  ])
}

function AnalyticsIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M3 13.125C3 12.504 3.504 12 4.125 12h2.25c.621 0 1.125.504 1.125 1.125v6.75C7.5 20.496 6.996 21 6.375 21h-2.25A1.125 1.125 0 013 19.875v-6.75zM9.75 8.625c0-.621.504-1.125 1.125-1.125h2.25c.621 0 1.125.504 1.125 1.125v11.25c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 01-1.125-1.125V8.625zM16.5 4.125c0-.621.504-1.125 1.125-1.125h2.25C20.496 3 21 3.504 21 4.125v15.75c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 01-1.125-1.125V4.125z' })
  ])
}

function SystemIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M9.594 3.94c.09-.542.56-.94 1.11-.94h2.593c.55 0 1.02.398 1.11.94l.213 1.281c.063.374.313.686.645.87.074.04.147.083.22.127.324.196.72.257 1.075.124l1.217-.456a1.125 1.125 0 011.37.49l1.296 2.247a1.125 1.125 0 01-.26 1.431l-1.003.827c-.293.24-.438.613-.431.992a6.759 6.759 0 010 .255c-.007.378.138.75.43.99l1.005.828c.424.35.534.954.26 1.43l-1.298 2.247a1.125 1.125 0 01-1.369.491l-1.217-.456c-.355-.133-.75-.072-1.076.124a6.57 6.57 0 01-.22.128c-.331.183-.581.495-.644.869l-.213 1.28c-.09.543-.56.941-1.11.941h-2.594c-.55 0-1.02-.398-1.11-.94l-.213-1.281c-.062-.374-.312-.686-.644-.87a6.52 6.52 0 01-.22-.127c-.325-.196-.72-.257-1.076-.124l-1.217.456a1.125 1.125 0 01-1.369-.49l-1.297-2.247a1.125 1.125 0 01.26-1.431l1.004-.827c.292-.24.437-.613.43-.992a6.932 6.932 0 010-.255c.007-.378-.138-.75-.43-.99l-1.004-.828a1.125 1.125 0 01-.26-1.43l1.297-2.247a1.125 1.125 0 011.37-.491l1.216.456c.356.133.751.072 1.076-.124.072-.044.146-.087.22-.128.332-.183.582-.495.644-.869l.214-1.281z' }),
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M15 12a3 3 0 11-6 0 3 3 0 016 0z' })
  ])
}

function RefreshIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0l3.181 3.183a8.25 8.25 0 0013.803-3.7M4.031 9.865a8.25 8.25 0 0113.803-3.7l3.181 3.182m0-4.991v4.99' })
  ])
}
</script>

