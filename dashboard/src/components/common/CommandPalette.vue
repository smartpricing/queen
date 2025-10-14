<template>
  <Teleport to="body">
      <div
        v-if="isOpen"
        class="fixed inset-0 z-50 overflow-y-auto"
        @click="close"
      >
        <!-- Backdrop -->
        <div class="fixed inset-0 bg-gray-900/80 backdrop-blur-sm"></div>
        
        <!-- Panel -->
        <div class="flex min-h-screen items-start justify-center p-4 pt-[10vh]">
          <div
            v-if="isOpen"
            class="relative w-full max-w-2xl bg-white dark:bg-gray-900 rounded-2xl shadow-2xl border border-gray-200 dark:border-gray-800 overflow-hidden"
            @click.stop
          >
              <!-- Search Input -->
              <div class="flex items-center gap-3 px-5 py-4 border-b border-gray-200 dark:border-gray-800">
                <svg class="w-5 h-5 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
                </svg>
                <input
                  ref="searchInput"
                  v-model="searchQuery"
                  type="text"
                  placeholder="Type a command or search..."
                  class="flex-1 bg-transparent border-none outline-none text-gray-900 dark:text-white text-base placeholder-gray-400"
                  @keydown.down.prevent="highlightNext"
                  @keydown.up.prevent="highlightPrevious"
                  @keydown.enter.prevent="executeHighlighted"
                  @keydown.escape="close"
                />
                <kbd class="hidden sm:inline-flex items-center gap-1 px-2 py-1 text-xs font-semibold text-gray-500 bg-gray-100 dark:bg-gray-800 border border-gray-300 dark:border-gray-700 rounded">
                  ESC
                </kbd>
              </div>
              
              <!-- Results -->
              <div class="max-h-96 overflow-y-auto">
                <div v-if="filteredCommands.length === 0" class="px-5 py-8 text-center">
                  <svg class="w-10 h-10 mx-auto text-gray-300 dark:text-gray-700 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.172 16.172a4 4 0 015.656 0M9 10h.01M15 10h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                  </svg>
                  <p class="text-sm text-gray-500 dark:text-gray-400">No commands found</p>
                </div>
                
                <div v-else class="py-2">
                  <!-- Group by category -->
                  <div v-for="category in categories" :key="category">
                    <div
                      v-if="getCommandsForCategory(category).length > 0"
                      class="px-5 py-2 text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider"
                    >
                      {{ category }}
                    </div>
                    <button
                      v-for="(command, index) in getCommandsForCategory(category)"
                      :key="command.id"
                      :class="[
                        'w-full flex items-center gap-3 px-5 py-3 text-left',
                        highlightedIndex === getGlobalIndex(category, index)
                          ? 'bg-primary-50 dark:bg-primary-900/20'
                          : 'hover:bg-gray-50 dark:hover:bg-gray-800/50'
                      ]"
                      @click="executeCommand(command)"
                      @mouseenter="highlightedIndex = getGlobalIndex(category, index)"
                    >
                      <div
                        :class="[
                          'flex items-center justify-center w-8 h-8 rounded-lg',
                          command.color === 'blue' && 'bg-blue-100 dark:bg-blue-900/30 text-blue-600 dark:text-blue-400',
                          command.color === 'green' && 'bg-emerald-100 dark:bg-emerald-900/30 text-emerald-600 dark:text-emerald-400',
                          command.color === 'purple' && 'bg-purple-100 dark:bg-purple-900/30 text-purple-600 dark:text-purple-400',
                          command.color === 'red' && 'bg-red-100 dark:bg-red-900/30 text-red-600 dark:text-red-400'
                        ]"
                      >
                        <component :is="command.icon" class="w-4 h-4" />
                      </div>
                      <div class="flex-1 min-w-0">
                        <div class="text-sm font-medium text-gray-900 dark:text-white">
                          {{ command.title }}
                        </div>
                        <div class="text-xs text-gray-500 dark:text-gray-400">
                          {{ command.description }}
                        </div>
                      </div>
                      <kbd v-if="command.shortcut" class="hidden sm:inline-flex items-center gap-1 px-2 py-1 text-xs font-mono text-gray-500 bg-gray-100 dark:bg-gray-800 border border-gray-300 dark:border-gray-700 rounded">
                        {{ command.shortcut }}
                      </kbd>
                    </button>
                  </div>
                </div>
              </div>
              
              <!-- Footer -->
              <div class="flex items-center justify-between px-5 py-3 border-t border-gray-200 dark:border-gray-800 bg-gray-50/50 dark:bg-gray-900/50">
                <div class="flex items-center gap-4 text-xs text-gray-500 dark:text-gray-400">
                  <div class="flex items-center gap-1.5">
                    <kbd class="px-1.5 py-0.5 bg-white dark:bg-gray-800 border border-gray-300 dark:border-gray-700 rounded text-[10px]">↑</kbd>
                    <kbd class="px-1.5 py-0.5 bg-white dark:bg-gray-800 border border-gray-300 dark:border-gray-700 rounded text-[10px]">↓</kbd>
                    <span>Navigate</span>
                  </div>
                  <div class="flex items-center gap-1.5">
                    <kbd class="px-1.5 py-0.5 bg-white dark:bg-gray-800 border border-gray-300 dark:border-gray-700 rounded text-[10px]">↵</kbd>
                    <span>Select</span>
                  </div>
                </div>
                <div class="text-xs text-gray-400">
                  Press <kbd class="px-1.5 py-0.5 bg-white dark:bg-gray-800 border border-gray-300 dark:border-gray-700 rounded text-[10px]">Cmd K</kbd> anytime
                </div>
              </div>
            </div>
        </div>
      </div>
  </Teleport>
</template>

<script setup>
import { ref, computed, watch, nextTick } from 'vue';
import { useRouter } from 'vue-router';
import {
  DashboardIcon,
  QueuesIcon,
  AnalyticsIcon,
  MessagesIcon,
  SettingsIcon,
  RefreshIcon,
  SearchIcon,
  ThemeIcon
} from '../icons/CommandIcons.js';

const props = defineProps({
  modelValue: {
    type: Boolean,
    default: false
  }
});

const emit = defineEmits(['update:modelValue', 'command']);

const router = useRouter();
const searchInput = ref(null);
const searchQuery = ref('');
const highlightedIndex = ref(0);

const isOpen = computed({
  get: () => props.modelValue,
  set: (value) => emit('update:modelValue', value)
});

// Commands
const commands = [
  // Navigation
  { id: 'nav-dashboard', category: 'Navigation', title: 'Go to Dashboard', description: 'View system overview', icon: DashboardIcon, color: 'blue', action: () => router.push('/'), shortcut: 'G D' },
  { id: 'nav-queues', category: 'Navigation', title: 'Go to Queues', description: 'Manage message queues', icon: QueuesIcon, color: 'purple', action: () => router.push('/queues'), shortcut: 'G Q' },
  { id: 'nav-analytics', category: 'Navigation', title: 'Go to Analytics', description: 'View performance metrics', icon: AnalyticsIcon, color: 'green', action: () => router.push('/analytics'), shortcut: 'G A' },
  { id: 'nav-messages', category: 'Navigation', title: 'Go to Messages', description: 'View message history', icon: MessagesIcon, color: 'blue', action: () => router.push('/messages'), shortcut: 'G M' },
  { id: 'nav-settings', category: 'Navigation', title: 'Go to Settings', description: 'Configure dashboard', icon: SettingsIcon, color: 'purple', action: () => router.push('/settings'), shortcut: 'G S' },
  
  // Actions
  { id: 'action-refresh', category: 'Actions', title: 'Refresh Data', description: 'Reload current view', icon: RefreshIcon, color: 'blue', action: () => { emit('command', 'refresh'); }, shortcut: 'R' },
  { id: 'action-search', category: 'Actions', title: 'Search Queues', description: 'Find specific queue', icon: SearchIcon, color: 'green', action: () => { emit('command', 'search'); }, shortcut: '/' },
  { id: 'action-theme', category: 'Actions', title: 'Toggle Theme', description: 'Switch light/dark mode', icon: ThemeIcon, color: 'purple', action: () => { emit('command', 'theme'); }, shortcut: 'T' }
];

const categories = ['Navigation', 'Actions'];

const filteredCommands = computed(() => {
  if (!searchQuery.value) return commands;
  
  const query = searchQuery.value.toLowerCase();
  return commands.filter(cmd => 
    cmd.title.toLowerCase().includes(query) ||
    cmd.description.toLowerCase().includes(query) ||
    cmd.category.toLowerCase().includes(query)
  );
});

const getCommandsForCategory = (category) => {
  return filteredCommands.value.filter(cmd => cmd.category === category);
};

const getGlobalIndex = (category, localIndex) => {
  let index = 0;
  for (const cat of categories) {
    if (cat === category) {
      return index + localIndex;
    }
    index += getCommandsForCategory(cat).length;
  }
  return 0;
};

const highlightNext = () => {
  highlightedIndex.value = Math.min(highlightedIndex.value + 1, filteredCommands.value.length - 1);
};

const highlightPrevious = () => {
  highlightedIndex.value = Math.max(highlightedIndex.value - 1, 0);
};

const executeHighlighted = () => {
  const command = filteredCommands.value[highlightedIndex.value];
  if (command) {
    executeCommand(command);
  }
};

const executeCommand = (command) => {
  command.action();
  close();
};

const close = () => {
  isOpen.value = false;
  searchQuery.value = '';
  highlightedIndex.value = 0;
};

watch(isOpen, async (value) => {
  if (value) {
    await nextTick();
    searchInput.value?.focus();
  }
});

watch(searchQuery, () => {
  highlightedIndex.value = 0;
});
</script>

