<template>
  <!-- Mobile overlay -->
  <div 
    v-if="isOpen" 
    @click="$emit('close')"
    class="fixed inset-0 bg-gray-900/50 backdrop-blur-sm z-40 lg:hidden"
  />
  
  <!-- Sidebar - Icon only on desktop, full width on mobile -->
  <aside 
    :class="[
      'fixed lg:static inset-y-0 left-0 z-50 bg-white dark:bg-gray-900 border-r border-gray-200 dark:border-gray-800 transform transition-all duration-300 ease-in-out overflow-hidden',
      isOpen ? 'translate-x-0 w-64' : '-translate-x-full lg:translate-x-0 lg:w-16',
    ]"
  >
    <div class="flex flex-col h-full overflow-hidden">
      <!-- Logo -->
      <div class="flex items-center justify-center py-3">
        <div class="w-10 h-10 flex items-center justify-center group cursor-pointer">
          <QueenLogo class="w-10 h-10 transition-transform duration-300 group-hover:scale-110" />
        </div>
      </div>
      
      <!-- Navigation -->
      <nav class="flex-1 py-3 space-y-1 overflow-y-auto overflow-x-hidden px-1">
        <router-link
          v-for="item in navigation"
          :key="item.path"
          :to="item.path"
          @click="$emit('close')"
          :class="[
            'group relative flex items-center justify-center w-10 h-10 mx-auto rounded-lg transition-all duration-200',
            isActive(item.path) ? activeNavClass : inactiveNavClass
          ]"
        >
          <component :is="item.icon" class="w-5 h-5 flex-shrink-0" />
          <span class="absolute left-full ml-2 px-3 py-1.5 bg-gray-900 dark:bg-gray-700 text-white text-sm rounded-lg whitespace-nowrap opacity-0 group-hover:opacity-100 transition-opacity pointer-events-none z-50 shadow-lg">
            {{ item.label }}
          </span>
        </router-link>
      </nav>
      
      <!-- Footer Controls -->
      <div class="p-1 border-t border-gray-200 dark:border-gray-800 space-y-1">
        <!-- Theme Toggle -->
        <button
          @click="toggleTheme"
          :class="['group relative flex items-center justify-center w-10 h-10 mx-auto rounded-lg text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-800 transition-all duration-200', hoverTextClass]"
          title="Toggle theme"
        >
          <svg v-if="colorMode === 'light'" class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z" />
          </svg>
          <svg v-else class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z" />
          </svg>
          <span class="absolute left-full ml-2 px-3 py-1.5 bg-gray-900 dark:bg-gray-700 text-white text-sm rounded-lg whitespace-nowrap opacity-0 group-hover:opacity-100 transition-opacity pointer-events-none z-50 shadow-lg">
            {{ colorMode === 'light' ? 'Dark Mode' : 'Light Mode' }}
          </span>
        </button>
        
        <!-- Mobile menu toggle (only visible on mobile) -->
        <button
          v-if="isOpen"
          @click="$emit('toggle-sidebar')"
          :class="['lg:hidden group relative flex items-center justify-center w-10 h-10 mx-auto rounded-lg text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-800 transition-all duration-200', hoverTextClass]"
        >
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
          </svg>
        </button>
      </div>
    </div>
  </aside>
</template>

<script setup>
import { computed } from 'vue';
import { useRoute } from 'vue-router';
import { useColorMode } from '../../composables/useColorMode';
import QueenLogo from '../icons/QueenLogo.vue';
import { theme } from '../../config/theme';

// Icons as render functions
import { h } from 'vue';

const HomeIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-6 0a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1m-6 0h6' })
  ])
};

const QueueIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M4 6h16M4 10h16M4 14h16M4 18h16' })
  ])
};

const AnalyticsIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z' })
  ])
};

const SettingsIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z' }),
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M15 12a3 3 0 11-6 0 3 3 0 016 0z' })
  ])
};

const props = defineProps({
  isOpen: {
    type: Boolean,
    default: false
  }
});

const emit = defineEmits(['close', 'toggle-sidebar']);
const route = useRoute();
const { colorMode, toggleColorMode } = useColorMode();

// Dynamic theme classes
const activeNavClass = computed(() => 
  `bg-gradient-to-br ${theme.classes.primaryGradient} text-white shadow-lg ${theme.classes.primaryShadow}`
);

const inactiveNavClass = computed(() => 
  `text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-800 ${theme.classes.primaryText.replace('text-', 'hover:text-').replace(' dark:text-', ' dark:hover:text-')}`
);

const hoverTextClass = computed(() => theme.classes.primaryText.replace('text-', 'hover:text-').replace(' dark:text-', ' dark:hover:text-'));

const toggleTheme = () => {
  toggleColorMode();
};

const navigation = [
  { path: '/', label: 'Dashboard', icon: HomeIcon },
  { path: '/queues', label: 'Queues', icon: QueueIcon },
  { path: '/analytics', label: 'Analytics', icon: AnalyticsIcon },
  { path: '/settings', label: 'Settings', icon: SettingsIcon }
];

const isActive = (path) => {
  if (path === '/') {
    return route.path === '/';
  }
  return route.path.startsWith(path);
};
</script>

