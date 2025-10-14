<template>
  <header class="bg-white/80 dark:bg-gray-900/80 backdrop-blur-md border-b border-gray-200 dark:border-gray-800 h-16 flex items-center px-4 lg:px-6 sticky top-0 z-30 shadow-sm">
    <div class="flex items-center justify-between w-full">
      <!-- Left section -->
      <div class="flex items-center space-x-4">
        <!-- Mobile menu button -->
        <button 
          @click="$emit('toggle-sidebar')"
          class="lg:hidden p-2 rounded-xl hover:bg-gray-100 dark:hover:bg-gray-800 text-gray-600 dark:text-gray-400 transition-all"
        >
          <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6h16M4 12h16M4 18h16" />
          </svg>
        </button>
        
        <!-- Page title with gradient accent -->
        <h1 class="text-xl font-bold bg-gradient-to-r from-primary-600 to-primary-500 bg-clip-text text-transparent">
          {{ pageTitle }}
        </h1>
      </div>
      
      <!-- Right section -->
      <div class="flex items-center space-x-2">
        <!-- Refresh button -->
        <button
          @click="handleRefresh"
          :class="[
            'p-2 rounded-xl hover:bg-primary-50 dark:hover:bg-primary-900/20 text-gray-600 dark:text-gray-400 hover:text-primary-600 dark:hover:text-primary-400 transition-all',
            { 'animate-spin': isRefreshing }
          ]"
          title="Refresh"
        >
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
          </svg>
        </button>
        
        <!-- Theme toggle -->
        <button
          @click="toggleTheme"
          class="p-2 rounded-xl hover:bg-primary-50 dark:hover:bg-primary-900/20 text-gray-600 dark:text-gray-400 hover:text-primary-600 dark:hover:text-primary-400 transition-all"
          title="Toggle theme"
        >
          <svg v-if="colorMode === 'light'" class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z" />
          </svg>
          <svg v-else class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z" />
          </svg>
        </button>
      </div>
    </div>
  </header>
</template>

<script setup>
import { ref, computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useColorMode } from '../../composables/useColorMode';

const emit = defineEmits(['toggle-sidebar']);
const route = useRoute();
const router = useRouter();
const { colorMode, toggleColorMode } = useColorMode();
const isRefreshing = ref(false);

const pageTitle = computed(() => route.meta.title || 'Queen Dashboard');

const toggleTheme = () => {
  toggleColorMode();
};

const handleRefresh = async () => {
  isRefreshing.value = true;
  
  // Trigger a route reload by navigating to the same route
  await router.replace({ path: route.path, query: { ...route.query, _refresh: Date.now() } });
  
  setTimeout(() => {
    isRefreshing.value = false;
  }, 500);
};
</script>

