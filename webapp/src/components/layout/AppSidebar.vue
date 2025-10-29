<template>
  <div class="flex flex-col h-full overflow-hidden">
    <!-- Logo section -->
    <div class="p-5 flex-shrink-0">
      <div 
        v-if="!isCollapsed" 
        class="flex items-center gap-3"
      >
        <img src="/assets/queen-logo.svg" alt="Queen" class="h-8 w-8 flex-shrink-0" />
        <div class="min-w-0">
          <h1 class="text-sm font-bold text-gray-900 dark:text-white truncate tracking-tight">Queen</h1>
          <p class="text-xs text-gray-500 dark:text-gray-400 truncate">Message Queue</p>
        </div>
      </div>
      <div 
        v-else 
        class="flex items-center justify-center"
      >
        <img src="/assets/queen-logo.svg" alt="Queen" class="h-8 w-8" />
      </div>
    </div>
    
    <!-- Navigation -->
    <nav class="flex-1 px-3 py-2 overflow-y-auto overflow-x-hidden">
      <router-link
        v-for="item in navigation"
        :key="item.path"
        :to="item.path"
        @click="$emit('close')"
        :title="item.name"
        class="nav-item group relative"
        :class="[
          isActive(item.path) ? 'nav-item-active' : 'nav-item-inactive',
          isCollapsed ? 'nav-item-collapsed' : 'nav-item-expanded'
        ]"
      >
        <svg 
          class="nav-icon" 
          fill="none" 
          stroke="currentColor" 
          viewBox="0 0 24 24"
          stroke-width="1.5"
        >
          <path stroke-linecap="round" stroke-linejoin="round" :d="item.iconPath" />
        </svg>
        
        <span v-if="!isCollapsed" class="nav-label">
          {{ item.name }}
        </span>
        
        <!-- Tooltip for collapsed state -->
        <div 
          v-if="isCollapsed"
          class="nav-tooltip"
        >
          {{ item.name }}
        </div>
      </router-link>
    </nav>
    
    <!-- Utilities Section (Desktop: theme, health, refresh, collapse) -->
    <div class="flex-shrink-0 hidden lg:block border-t border-gray-200/60 dark:border-gray-800/60">
      <div class="px-3 py-3">
        <!-- Refresh Button -->
        <button
          v-if="showRefresh"
          @click="handleRefresh"
          :disabled="isRefreshing"
          :title="isCollapsed ? 'Refresh' : ''"
          class="utility-btn group"
          :class="isCollapsed ? 'utility-btn-collapsed' : 'utility-btn-expanded'"
        >
          <svg 
            class="utility-icon"
            :class="{ 'animate-spin': isRefreshing }"
            fill="none" 
            stroke="currentColor" 
            viewBox="0 0 24 24"
            stroke-width="1.5"
          >
            <path stroke-linecap="round" stroke-linejoin="round" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
          </svg>
          <span v-if="!isCollapsed" class="utility-label">Refresh</span>
          
          <div v-if="isCollapsed" class="nav-tooltip">
            Refresh
          </div>
        </button>
        
        <!-- Theme Toggle -->
        <button
          @click="toggleTheme"
          :title="isCollapsed ? (isDark ? 'Light mode' : 'Dark mode') : ''"
          class="utility-btn group"
          :class="isCollapsed ? 'utility-btn-collapsed' : 'utility-btn-expanded'"
        >
          <svg class="utility-icon" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.5">
            <path v-if="isDark" stroke-linecap="round" stroke-linejoin="round" d="M12 3v2.25m6.364.386l-1.591 1.591M21 12h-2.25m-.386 6.364l-1.591-1.591M12 18.75V21m-4.773-4.227l-1.591 1.591M5.25 12H3m4.227-4.773L5.636 5.636M15.75 12a3.75 3.75 0 11-7.5 0 3.75 3.75 0 017.5 0z"/>
            <path v-else stroke-linecap="round" stroke-linejoin="round" d="M21.752 15.002A9.718 9.718 0 0118 15.75c-5.385 0-9.75-4.365-9.75-9.75 0-1.33.266-2.597.748-3.752A9.753 9.753 0 003 11.25C3 16.635 7.365 21 12.75 21a9.753 9.753 0 009.002-5.998z"/>
          </svg>
          <span v-if="!isCollapsed" class="utility-label">{{ isDark ? 'Light Mode' : 'Dark Mode' }}</span>
          
          <div v-if="isCollapsed" class="nav-tooltip">
            {{ isDark ? 'Light mode' : 'Dark mode' }}
          </div>
        </button>
        
        <!-- Health Status -->
        <div
          :title="isCollapsed ? (isHealthy ? 'System Healthy' : 'System Error') : ''"
          class="utility-status group"
          :class="[
            isHealthy ? 'text-emerald-500 dark:text-emerald-400' : 'text-red-500 dark:text-red-400',
            isCollapsed ? 'utility-btn-collapsed' : 'utility-btn-expanded'
          ]"
        >
          <svg class="utility-icon" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.5">
            <path v-if="isHealthy" stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            <path v-else stroke-linecap="round" stroke-linejoin="round" d="M9.75 9.75l4.5 4.5m0-4.5l-4.5 4.5M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          <span v-if="!isCollapsed" class="utility-label font-medium">{{ isHealthy ? 'Healthy' : 'Error' }}</span>
          
          <div v-if="isCollapsed" class="nav-tooltip">
            {{ isHealthy ? 'System Healthy' : 'System Error' }}
          </div>
        </div>
        
        <!-- Collapse Button -->
        <button 
          @click="$emit('toggle-collapse')"
          class="utility-btn group"
          :class="isCollapsed ? 'utility-btn-collapsed' : 'utility-btn-expanded'"
          :title="isCollapsed ? 'Expand sidebar' : 'Collapse sidebar'"
        >
          <svg 
            class="utility-icon transition-transform duration-200" 
            :class="{ 'rotate-180': isCollapsed }"
            fill="none" 
            stroke="currentColor" 
            viewBox="0 0 24 24"
            stroke-width="1.5"
          >
            <path stroke-linecap="round" stroke-linejoin="round" d="M18.75 19.5l-7.5-7.5 7.5-7.5m-6 15L5.25 12l7.5-7.5" />
          </svg>
          <span v-if="!isCollapsed" class="utility-label">Collapse</span>
          
          <div v-if="isCollapsed" class="nav-tooltip">
            Expand sidebar
          </div>
        </button>
      </div>
    </div>
    
    <!-- Mobile utilities (theme and health in header area when sidebar open) -->
    <div v-if="isMobile && isSidebarOpen" class="lg:hidden p-3 border-t border-gray-200 dark:border-gray-700">
      <div class="flex gap-2">
        <button
          @click="toggleTheme"
          class="flex-1 btn btn-secondary text-xs py-2"
        >
          {{ isDark ? '☀️ Light' : '🌙 Dark' }}
        </button>
        <div class="px-3 py-2 bg-gray-100 dark:bg-slate-700 rounded-lg text-xs font-medium flex items-center gap-2">
          <span :class="isHealthy ? 'text-green-600' : 'text-red-600'">
            {{ isHealthy ? '✓' : '✗' }}
          </span>
          {{ isHealthy ? 'Healthy' : 'Error' }}
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, inject, computed, onMounted } from 'vue';
import { useRoute } from 'vue-router';
import { healthApi } from '../../api/health';

defineProps({
  isCollapsed: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits(['close', 'toggle-collapse', 'toggle-sidebar-mobile']);

const route = useRoute();
const { isDark, toggleTheme } = inject('theme');

// Health check
const health = ref(null);
const isHealthy = computed(() => health.value?.status === 'healthy');
const isRefreshing = ref(false);

async function checkHealth() {
  try {
    const response = await healthApi.getHealth();
    health.value = response.data;
  } catch (error) {
    health.value = { status: 'unhealthy' };
  }
}

// Refresh logic
const showRefresh = computed(() => {
  return ['/', '/queues', '/consumer-groups', '/messages', '/traces', '/analytics', '/system-metrics'].includes(route.path);
});

function handleRefresh() {
  isRefreshing.value = true;
  
  const refreshCallbacks = window.refreshCallbacks || new Map();
  const callback = refreshCallbacks.get(route.path);
  
  if (callback) {
    Promise.resolve(callback())
      .finally(() => {
        setTimeout(() => {
          isRefreshing.value = false;
        }, 300);
      });
  } else {
    setTimeout(() => {
      isRefreshing.value = false;
    }, 300);
  }
}

onMounted(() => {
  checkHealth();
  setInterval(checkHealth, 30000);
  
  // Initialize global callbacks if not exists
  if (!window.refreshCallbacks) {
    window.refreshCallbacks = new Map();
  }
  
  window.registerRefreshCallback = (path, callback) => {
    window.refreshCallbacks.set(path, callback);
  };
});

const navigation = [
  { 
    name: 'Dashboard', 
    path: '/',
    iconPath: 'M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-6 0a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1m-6 0h6'
  },
  { 
    name: 'Queues', 
    path: '/queues',
    iconPath: 'M4 6h16M4 10h16M4 14h16M4 18h16'
  },
  { 
    name: 'Consumer Groups', 
    path: '/consumer-groups',
    iconPath: 'M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z'
  },
  { 
    name: 'Messages', 
    path: '/messages',
    iconPath: 'M8 10h.01M12 10h.01M16 10h.01M9 16H5a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v8a2 2 0 01-2 2h-5l-5 5v-5z'
  },
  { 
    name: 'Traces', 
    path: '/traces',
    iconPath: 'M9 20l-5.447-2.724A1 1 0 013 16.382V5.618a1 1 0 011.447-.894L9 7m0 13l6-3m-6 3V7m6 10l4.553 2.276A1 1 0 0021 18.382V7.618a1 1 0 00-.553-.894L15 4m0 13V4m0 0L9 7'
  },
  { 
    name: 'Analytics', 
    path: '/analytics',
    iconPath: 'M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z'
  },
  { 
    name: 'System Metrics', 
    path: '/system-metrics',
    iconPath: 'M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z'
  },
];

const isActive = (path) => {
  if (path === '/') return route.path === '/';
  return route.path.startsWith(path);
};
</script>

<style scoped>
/* Professional Navigation Styles - Condoktur inspired */

.nav-item {
  @apply flex items-center mb-1 text-sm font-medium;
  @apply rounded-lg;
  transition: background-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), color 0.15s cubic-bezier(0.4, 0, 0.2, 1);
}

.nav-item-expanded {
  @apply px-3 py-2.5 gap-3;
}

.nav-item-collapsed {
  @apply px-3 py-2.5 justify-center;
}

.nav-item-active {
  @apply bg-gray-100 dark:bg-gray-800/70 text-gray-900 dark:text-white;
  font-weight: 600;
  box-shadow: 0 1px 2px 0 rgba(0, 0, 0, 0.05);
}

.dark .nav-item-active {
  box-shadow: 0 1px 2px 0 rgba(0, 0, 0, 0.3);
}

.nav-item-inactive {
  @apply text-gray-600 dark:text-gray-400 hover:bg-gray-50 dark:hover:bg-gray-800/50 hover:text-gray-900 dark:hover:text-gray-100;
}

.nav-icon {
  @apply w-5 h-5 flex-shrink-0;
}

.nav-label {
  @apply whitespace-nowrap overflow-hidden text-ellipsis;
}

.nav-tooltip {
  @apply absolute left-full ml-3 px-3 py-1.5 bg-gray-900 dark:bg-gray-800 text-white text-xs rounded-lg;
  @apply opacity-0 pointer-events-none group-hover:opacity-100 whitespace-nowrap z-50;
  transition: opacity 0.15s cubic-bezier(0.4, 0, 0.2, 1);
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.3), 0 2px 4px -1px rgba(0, 0, 0, 0.2);
}

/* Utility Button Styles */
.utility-btn {
  @apply w-full flex items-center mb-1 rounded-lg text-sm font-medium text-gray-600 dark:text-gray-400;
  @apply hover:bg-gray-50 dark:hover:bg-gray-800/50 hover:text-gray-900 dark:hover:text-gray-100;
  transition: background-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), color 0.15s cubic-bezier(0.4, 0, 0.2, 1);
}

.utility-btn-expanded {
  @apply px-3 py-2.5 gap-3;
}

.utility-btn-collapsed {
  @apply px-3 py-2.5 justify-center;
}

.utility-status {
  @apply w-full flex items-center mb-1 rounded-lg text-sm font-medium;
}

.utility-icon {
  @apply w-5 h-5 flex-shrink-0;
}

.utility-label {
  @apply whitespace-nowrap overflow-hidden text-ellipsis;
}
</style>
