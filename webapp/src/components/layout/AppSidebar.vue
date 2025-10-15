<template>
  <div class="flex flex-col h-full overflow-hidden">
    <!-- Logo section -->
    <div class="p-3 flex-shrink-0">
      <div 
        v-if="!isCollapsed" 
        class="flex items-center gap-3"
      >
        <img src="/assets/queen-logo-rose.svg" alt="Queen" class="h-8 w-8 flex-shrink-0" />
        <div class="min-w-0">
          <h1 class="text-base font-bold text-gray-900 dark:text-gray-100 truncate">Queen</h1>
          <p class="text-xs text-gray-500 dark:text-gray-400 truncate">Message Queue</p>
        </div>
      </div>
      <div 
        v-else 
        class="flex items-center justify-center"
      >
        <img src="/assets/queen-logo-rose.svg" alt="Queen" class="h-8 w-8" />
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
        class="flex items-center rounded-lg transition-all text-sm group relative"
        :class="[
          isActive(item.path)
            ? 'bg-rose-50 dark:bg-rose-900/20 text-rose-600 dark:text-rose-400 font-medium'
            : 'text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-slate-700',
          isCollapsed ? 'px-3 py-3 justify-center' : 'px-3 py-2.5 gap-3'
        ]"
      >
        <svg 
          class="flex-shrink-0 transition-transform group-hover:scale-110 w-5 h-5" 
          fill="none" 
          stroke="currentColor" 
          viewBox="0 0 24 24"
        >
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" :d="item.iconPath" />
        </svg>
        
        <span 
          v-if="!isCollapsed" 
          class="whitespace-nowrap overflow-hidden text-ellipsis"
        >
          {{ item.name }}
        </span>
        
        <!-- Tooltip for collapsed state -->
        <div 
          v-if="isCollapsed"
          class="absolute left-full ml-2 px-2 py-1 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded opacity-0 pointer-events-none group-hover:opacity-100 transition-opacity whitespace-nowrap z-50"
        >
          {{ item.name }}
        </div>
      </router-link>
    </nav>
    
    <!-- Collapse Toggle Button (Desktop only) -->
    <div class="p-3 border-t border-gray-200 dark:border-gray-700 flex-shrink-0 hidden lg:block">
      <button 
        @click="$emit('toggle-collapse')"
        class="w-full flex items-center rounded-lg transition-all text-sm text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-slate-700 group"
        :class="isCollapsed ? 'px-3 py-3 justify-center' : 'px-3 py-2.5 gap-3'"
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
        
        <span v-if="!isCollapsed" class="whitespace-nowrap">
          Collapse
        </span>
        
        <!-- Tooltip for collapsed state -->
        <div 
          v-if="isCollapsed"
          class="absolute left-full ml-2 px-2 py-1 bg-gray-900 dark:bg-gray-700 text-white text-xs rounded opacity-0 pointer-events-none group-hover:opacity-100 transition-opacity whitespace-nowrap z-50"
        >
          Expand sidebar
        </div>
      </button>
    </div>
  </div>
</template>

<script setup>
import { useRoute } from 'vue-router';

defineProps({
  isCollapsed: {
    type: Boolean,
    default: false,
  },
});

defineEmits(['close', 'toggle-collapse']);

const route = useRoute();

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
