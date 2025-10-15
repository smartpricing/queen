<template>
  <header class="h-14 flex items-center px-4 sm:px-6 justify-between flex-shrink-0">
    <div class="flex items-center gap-3 flex-1 min-w-0">
      <!-- Hamburger menu for mobile only -->
      <button 
        @click="$emit('toggle-sidebar')"
        class="lg:hidden p-2 -ml-2 rounded-md hover:bg-gray-100 dark:hover:bg-slate-700 transition-colors"
        aria-label="Toggle menu"
      >
        <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6h16M4 12h16M4 18h16" />
        </svg>
      </button>
      
      <!-- Page title -->
      <div class="flex items-center gap-2 min-w-0">
        <h1 class="text-lg font-bold text-gray-900 dark:text-gray-100 truncate">
          {{ pageTitle }}
        </h1>
      </div>
    </div>
    
    <div class="flex items-center gap-2">
      <RefreshButton v-if="showRefresh" :loading="isRefreshing" @refresh="handleRefresh" />
      <HealthIndicator />
      <ThemeToggle />
    </div>
  </header>
</template>

<script setup>
import { computed, ref, watch } from 'vue';
import { useRoute } from 'vue-router';
import HealthIndicator from '../dashboard/HealthIndicator.vue';
import ThemeToggle from './ThemeToggle.vue';
import RefreshButton from '../common/RefreshButton.vue';

const props = defineProps({
  isSidebarCollapsed: Boolean,
});

defineEmits(['toggle-sidebar']);

const route = useRoute();
const isRefreshing = ref(false);

const pageTitle = computed(() => {
  const titles = {
    '/': 'Dashboard',
    '/queues': 'Queues',
    '/consumer-groups': 'Consumer Groups',
    '/messages': 'Messages',
    '/analytics': 'Analytics',
  };
  
  // For dynamic routes like /queues/:queueName
  if (route.path.startsWith('/queues/') && route.path !== '/queues') {
    return `Queue: ${route.params.queueName}`;
  }
  
  return titles[route.path] || 'Queen Message Queue';
});

const showRefresh = computed(() => {
  // Show refresh button on pages that need it
  return ['/', '/queues', '/consumer-groups', '/messages', '/analytics'].includes(route.path);
});

// Global refresh event bus
const refreshCallbacks = new Map();

function handleRefresh() {
  isRefreshing.value = true;
  
  // Emit event that the current page can listen to
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

// Register refresh callback
window.registerRefreshCallback = (path, callback) => {
  refreshCallbacks.set(path, callback);
};

// Clean up on route change
watch(() => route.path, () => {
  isRefreshing.value = false;
});
</script>
