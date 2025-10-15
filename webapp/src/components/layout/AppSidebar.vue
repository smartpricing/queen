<template>
  <div class="flex flex-col h-full overflow-hidden">
    <!-- Logo section -->
    <div class="p-3 flex-shrink-0 mb-2">
      <div 
        v-if="!isCollapsed" 
        class="flex items-center gap-3"
      >
        <div class="relative">
          <img src="/assets/queen-logo-rose.svg" alt="Queen" class="h-8 w-8 flex-shrink-0 transition-transform hover:scale-110" />
          <div class="absolute inset-0 bg-rose-500 opacity-0 blur-xl transition-opacity hover:opacity-20 -z-10"></div>
        </div>
        <div class="min-w-0">
          <h1 class="text-base font-bold text-gray-900 dark:text-gray-100 truncate">Queen</h1>
          <p class="text-xs text-gray-500 dark:text-gray-400 truncate">Message Queue</p>
        </div>
      </div>
      <div 
        v-else 
        class="flex items-center justify-center relative"
      >
        <img src="/assets/queen-logo-rose.svg" alt="Queen" class="h-8 w-8 transition-transform hover:scale-110" />
        <div class="absolute inset-0 bg-rose-500 opacity-0 blur-xl transition-opacity hover:opacity-20 -z-10"></div>
      </div>
    </div>
    
    <!-- Navigation -->
    <nav class="flex-1 p-3 space-y-1 overflow-y-auto overflow-x-hidden scrollbar-thin">
      <router-link
        v-for="item in navigation"
        :key="item.path"
        :to="item.path"
        @click="$emit('close')"
        :title="item.name"
        class="flex items-center rounded-lg transition-all duration-200 text-sm group relative"
        :class="[
          isActive(item.path)
            ? 'bg-rose-500/8 dark:bg-rose-500/10 text-rose-600 dark:text-rose-400 font-medium'
            : 'text-gray-700 dark:text-gray-300 hover:bg-black/5 dark:hover:bg-white/5',
          isCollapsed ? 'px-3 py-3 justify-center' : 'px-3 py-2.5 gap-3'
        ]"
      >
        <div :class="[
          'flex items-center justify-center rounded-md transition-all',
          isActive(item.path) && !isCollapsed ? 'w-8 h-8 bg-gradient-to-br from-rose-500 to-purple-500 text-white shadow-sm' : ''
        ]">
          <svg 
            class="flex-shrink-0 transition-transform group-hover:scale-110 w-5 h-5" 
            fill="none" 
            stroke="currentColor" 
            viewBox="0 0 24 24"
          >
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" :d="item.iconPath" />
          </svg>
        </div>
        
        <span 
          v-if="!isCollapsed" 
          class="whitespace-nowrap overflow-hidden text-ellipsis"
        >
          {{ item.name }}
        </span>
        
        <!-- Tooltip for collapsed state -->
        <div 
          v-if="isCollapsed"
          class="absolute left-full ml-2 px-3 py-1.5 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg opacity-0 pointer-events-none group-hover:opacity-100 transition-opacity whitespace-nowrap z-50 shadow-lg"
        >
          {{ item.name }}
        </div>
      </router-link>
    </nav>
    
    <!-- Utilities Section (Desktop: theme, health, refresh, collapse) -->
    <div class="flex-shrink-0 hidden lg:block">
      <div class="h-px bg-gradient-to-r from-transparent via-gray-300 dark:via-gray-700 to-transparent mb-2"></div>
      
      <div class="p-3 space-y-1">
        <!-- Refresh Button -->
        <button
          v-if="showRefresh"
          @click="handleRefresh"
          :disabled="isRefreshing"
          :title="isCollapsed ? 'Refresh' : ''"
          class="w-full flex items-center rounded-lg transition-all duration-200 text-sm text-gray-700 dark:text-gray-300 hover:bg-black/5 dark:hover:bg-white/5 group relative"
          :class="isCollapsed ? 'px-3 py-2.5 justify-center' : 'px-3 py-2.5 gap-3'"
        >
          <svg 
            class="w-5 h-5 flex-shrink-0 transition-transform"
            :class="{ 'animate-spin': isRefreshing, 'group-hover:rotate-90': !isRefreshing }"
            fill="none" 
            stroke="currentColor" 
            viewBox="0 0 24 24"
          >
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
          </svg>
          <span v-if="!isCollapsed">Refresh</span>
          
          <div 
            v-if="isCollapsed"
            class="absolute left-full ml-2 px-3 py-1.5 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg opacity-0 pointer-events-none group-hover:opacity-100 transition-opacity whitespace-nowrap z-50 shadow-lg"
          >
            Refresh
          </div>
        </button>
        
        <!-- Theme Toggle -->
        <button
          @click="toggleTheme"
          :title="isCollapsed ? (isDark ? 'Light mode' : 'Dark mode') : ''"
          class="w-full flex items-center rounded-lg transition-all duration-200 text-sm text-gray-700 dark:text-gray-300 hover:bg-black/5 dark:hover:bg-white/5 group relative"
          :class="isCollapsed ? 'px-3 py-2.5 justify-center' : 'px-3 py-2.5 gap-3'"
        >
          <svg class="w-5 h-5 flex-shrink-0" fill="currentColor" viewBox="0 0 24 24">
            <path v-if="isDark" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z"/>
            <path v-else d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8z" opacity="0.5"/>
          </svg>
          <span v-if="!isCollapsed">{{ isDark ? 'Light Mode' : 'Dark Mode' }}</span>
          
          <div 
            v-if="isCollapsed"
            class="absolute left-full ml-2 px-3 py-1.5 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg opacity-0 pointer-events-none group-hover:opacity-100 transition-opacity whitespace-nowrap z-50 shadow-lg"
          >
            {{ isDark ? 'Light mode' : 'Dark mode' }}
          </div>
        </button>
        
        <!-- Health Status -->
        <div
          :title="isCollapsed ? (isHealthy ? 'System Healthy' : 'System Error') : ''"
          class="w-full flex items-center rounded-lg text-sm group relative"
          :class="[
            isHealthy ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400',
            isCollapsed ? 'px-3 py-2.5 justify-center' : 'px-3 py-2.5 gap-3'
          ]"
        >
          <svg class="w-5 h-5 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path v-if="isHealthy" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
            <path v-else stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 14l2-2m0 0l2-2m-2 2l-2-2m2 2l2 2m7-2a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          <span v-if="!isCollapsed" class="font-medium">{{ isHealthy ? 'Healthy' : 'Error' }}</span>
          
          <div 
            v-if="isCollapsed"
            class="absolute left-full ml-2 px-3 py-1.5 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg opacity-0 pointer-events-none group-hover:opacity-100 transition-opacity whitespace-nowrap z-50 shadow-lg"
          >
            {{ isHealthy ? 'System Healthy' : 'System Error' }}
          </div>
        </div>
        
        <!-- Collapse Button -->
        <button 
          @click="$emit('toggle-collapse')"
          class="w-full flex items-center rounded-lg transition-all duration-200 text-sm text-gray-700 dark:text-gray-300 hover:bg-black/5 dark:hover:bg-white/5 group relative"
          :class="isCollapsed ? 'px-3 py-2.5 justify-center' : 'px-3 py-2.5 gap-3'"
          :title="isCollapsed ? 'Expand sidebar' : 'Collapse sidebar'"
        >
          <svg 
            class="w-5 h-5 transition-transform duration-300 flex-shrink-0" 
            :class="{ 'rotate-180': isCollapsed }"
            fill="none" 
            stroke="currentColor" 
            viewBox="0 0 24 24"
          >
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 19l-7-7 7-7m8 14l-7-7 7-7" />
          </svg>
          <span v-if="!isCollapsed" class="whitespace-nowrap">Collapse</span>
          
          <div 
            v-if="isCollapsed"
            class="absolute left-full ml-2 px-3 py-1.5 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded-lg opacity-0 pointer-events-none group-hover:opacity-100 transition-opacity whitespace-nowrap z-50 shadow-lg"
          >
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
  return ['/', '/queues', '/consumer-groups', '/messages', '/analytics'].includes(route.path);
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
    name: 'Analytics', 
    path: '/analytics',
    iconPath: 'M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z'
  },
];

const isActive = (path) => {
  if (path === '/') return route.path === '/';
  return route.path.startsWith(path);
};
</script>
