<template>
  <AppLayout>
    <div class="max-w-4xl mx-auto space-y-6">
      <h1 class="text-3xl font-bold text-gray-900 dark:text-white">
        Settings
      </h1>
      
      <!-- Appearance -->
      <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6">
        <h2 class="text-lg font-semibold text-gray-900 dark:text-white mb-4">
          Appearance
        </h2>
        
        <div class="space-y-6">
          <!-- Color Theme Selection -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">
              Color Theme
            </label>
            <div class="grid grid-cols-2 md:grid-cols-4 gap-4">
              <button
                v-for="(themeOption, key) in availableThemes"
                :key="key"
                @click="selectTheme(key)"
                :class="[
                  'p-4 rounded-lg border-2 transition-all hover:scale-105',
                  currentTheme === key
                    ? 'border-primary-500 bg-primary-50 dark:bg-primary-900/20 shadow-lg'
                    : 'border-gray-200 dark:border-gray-700 hover:border-gray-300 dark:hover:border-gray-600'
                ]"
              >
                <div class="flex flex-col items-center space-y-2">
                  <!-- Color circle preview -->
                  <div class="w-10 h-10 rounded-full" :style="{ backgroundColor: themeOption.primary[500] }"></div>
                  <span class="text-sm font-medium text-gray-900 dark:text-white">
                    {{ themeOption.label }}
                  </span>
                  <span class="text-xs text-gray-500 dark:text-gray-400 text-center">
                    {{ themeOption.description }}
                  </span>
                </div>
              </button>
            </div>
          </div>

          <!-- Light/Dark Mode -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">
              Display Mode
            </label>
            <div class="grid grid-cols-3 gap-4">
              <button
                v-for="mode in ['light', 'dark', 'system']"
                :key="mode"
                @click="setColorMode(mode)"
                :class="[
                  'p-4 rounded-lg border-2 transition-all',
                  preferredMode === mode
                    ? 'border-primary-500 bg-primary-50 dark:bg-primary-900/20'
                    : 'border-gray-200 dark:border-gray-700 hover:border-gray-300 dark:hover:border-gray-600'
                ]"
              >
                <div class="flex flex-col items-center space-y-2">
                  <svg v-if="mode === 'light'" class="w-8 h-8" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z" />
                  </svg>
                  <svg v-else-if="mode === 'dark'" class="w-8 h-8" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z" />
                  </svg>
                  <svg v-else class="w-8 h-8" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" />
                  </svg>
                  <span class="text-sm font-medium text-gray-900 dark:text-white capitalize">
                    {{ mode }}
                  </span>
                </div>
              </button>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Auto-Refresh -->
      <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6">
        <h2 class="text-lg font-semibold text-gray-900 dark:text-white mb-4">
          Auto-Refresh
        </h2>
        
        <div class="space-y-4">
          <!-- Enable Auto-Refresh -->
          <div class="flex items-center justify-between">
            <div>
              <label class="text-sm font-medium text-gray-900 dark:text-white">
                Enable Auto-Refresh
              </label>
              <p class="text-sm text-gray-500 dark:text-gray-400 mt-1">
                Automatically refresh data at regular intervals
              </p>
            </div>
            <button
              @click="settings.autoRefresh = !settings.autoRefresh"
              :class="[
                'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
                settings.autoRefresh ? 'bg-primary-600' : 'bg-gray-200 dark:bg-gray-700'
              ]"
            >
              <span
                :class="[
                  'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                  settings.autoRefresh ? 'translate-x-6' : 'translate-x-1'
                ]"
              />
            </button>
          </div>
          
          <!-- Refresh Interval -->
          <div v-if="settings.autoRefresh">
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">
              Refresh Interval: {{ settings.refreshInterval / 1000 }}s
            </label>
            <input
              v-model.number="settings.refreshInterval"
              type="range"
              min="5000"
              max="60000"
              step="5000"
              class="w-full h-2 bg-gray-200 dark:bg-gray-700 rounded-lg appearance-none cursor-pointer accent-green-600"
            />
            <div class="flex justify-between text-xs text-gray-500 dark:text-gray-400 mt-1">
              <span>5s</span>
              <span>30s</span>
              <span>60s</span>
            </div>
          </div>
        </div>
      </div>
      
      <!-- API Configuration -->
      <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6">
        <h2 class="text-lg font-semibold text-gray-900 dark:text-white mb-4">
          API Configuration
        </h2>
        
        <div class="space-y-4">
          <!-- API Base URL -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              API Base URL
            </label>
            <input
              v-model="settings.apiBaseUrl"
              type="text"
              placeholder="http://localhost:6632"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent"
            />
            <p class="mt-2 text-sm text-gray-500 dark:text-gray-400">
              Current: http://localhost:6632
            </p>
          </div>
          
          <!-- Connection Status -->
          <div class="p-4 bg-primary-50 dark:bg-primary-900/20 rounded-lg border border-primary-200 dark:border-primary-800">
            <div class="flex items-center">
              <div class="w-3 h-3 bg-green-500 rounded-full mr-3"></div>
              <div>
                <div class="text-sm font-medium text-primary-800 dark:text-primary-300">
                  Connected
                </div>
                <div class="text-xs text-primary-600 dark:text-primary-400 mt-1">
                  API server is responding
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Display Preferences -->
      <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6">
        <h2 class="text-lg font-semibold text-gray-900 dark:text-white mb-4">
          Display Preferences
        </h2>
        
        <div class="space-y-4">
          <!-- Time Format -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Time Format
            </label>
            <select
              v-model="settings.timeFormat"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent"
            >
              <option value="relative">Relative (e.g., "2 hours ago")</option>
              <option value="absolute">Absolute (e.g., "2023-10-15 14:30:00")</option>
            </select>
          </div>
          
          <!-- Table Rows Per Page -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Table Rows Per Page
            </label>
            <select
              v-model.number="settings.rowsPerPage"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent"
            >
              <option :value="25">25</option>
              <option :value="50">50</option>
              <option :value="100">100</option>
              <option :value="250">250</option>
            </select>
          </div>
        </div>
      </div>
      
      <!-- Actions -->
      <div class="flex justify-between">
        <button
          @click="resetSettings"
          class="px-6 py-2 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 text-gray-900 dark:text-white rounded-lg transition-colors"
        >
          Reset to Defaults
        </button>
        
        <button
          @click="saveSettings"
          class="px-6 py-2 bg-primary-600 hover:bg-primary-700 text-white rounded-lg transition-colors"
        >
          Save Settings
        </button>
      </div>
      
      <!-- Success Message -->
      <div 
        v-if="showSuccess"
        class="p-4 bg-primary-50 dark:bg-primary-900/20 rounded-lg border border-primary-200 dark:border-primary-800"
      >
        <div class="flex items-center">
          <svg class="w-5 h-5 text-green-500 mr-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
          </svg>
          <span class="text-sm font-medium text-primary-800 dark:text-primary-300">
            Settings saved successfully
          </span>
        </div>
      </div>
    </div>
  </AppLayout>
</template>

<script setup>
import { ref, onMounted } from 'vue';
import AppLayout from '../components/layout/AppLayout.vue';
import { useColorMode } from '../composables/useColorMode';
import { useTheme } from '../composables/useTheme';

const { preferredMode, setColorMode } = useColorMode();
const { currentTheme, availableThemes, setTheme } = useTheme();

const selectTheme = (themeName) => {
  setTheme(themeName);
};

const settings = ref({
  autoRefresh: true,
  refreshInterval: 5000,
  apiBaseUrl: 'http://localhost:6632',
  timeFormat: 'relative',
  rowsPerPage: 50
});

const showSuccess = ref(false);

const defaultSettings = {
  autoRefresh: true,
  refreshInterval: 5000,
  apiBaseUrl: 'http://localhost:6632',
  timeFormat: 'relative',
  rowsPerPage: 50
};

const loadSettings = () => {
  const saved = localStorage.getItem('queen-settings');
  if (saved) {
    try {
      settings.value = { ...defaultSettings, ...JSON.parse(saved) };
    } catch (err) {
      console.error('Failed to load settings:', err);
    }
  }
};

const saveSettings = () => {
  localStorage.setItem('queen-settings', JSON.stringify(settings.value));
  showSuccess.value = true;
  
  setTimeout(() => {
    showSuccess.value = false;
  }, 3000);
};

const resetSettings = () => {
  settings.value = { ...defaultSettings };
  saveSettings();
};

onMounted(() => {
  loadSettings();
});
</script>

