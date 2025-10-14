<template>
  <AppLayout>
    <div class="max-w-5xl mx-auto space-y-6">
      <!-- Header -->
      <div>
        <h1 class="text-3xl font-black text-gray-900 dark:text-white mb-1">
          Settings
        </h1>
        <p class="text-sm text-gray-600 dark:text-gray-400">
          Customize your dashboard experience
        </p>
      </div>
      
      <!-- Appearance -->
      <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
        <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6 flex items-center gap-2">
          <svg class="w-6 h-6 text-primary-600 dark:text-primary-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M7 21a4 4 0 01-4-4V5a2 2 0 012-2h4a2 2 0 012 2v12a4 4 0 01-4 4zm0 0h12a2 2 0 002-2v-4a2 2 0 00-2-2h-2.343M11 7.343l1.657-1.657a2 2 0 012.828 0l2.829 2.829a2 2 0 010 2.828l-8.486 8.485M7 17h.01" />
          </svg>
          Appearance
        </h2>
        
        <div class="space-y-6">
          <!-- Color Theme Selection -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-4">
              Color Theme
            </label>
            <div class="grid grid-cols-2 md:grid-cols-4 gap-4">
              <button
                v-for="(themeOption, key) in availableThemes"
                :key="key"
                @click="selectTheme(key)"
                :class="[
                  'p-5 rounded-xl border-2 transition-all duration-200 hover:scale-105 active:scale-95',
                  currentTheme === key
                    ? 'border-primary-500 bg-primary-50 dark:bg-primary-900/30 shadow-lg shadow-primary-500/20'
                    : 'border-gray-200 dark:border-gray-700 hover:border-gray-300 dark:hover:border-gray-600'
                ]"
              >
                <div class="flex flex-col items-center space-y-3">
                  <div 
                    class="w-14 h-14 rounded-xl shadow-lg"
                    :style="{ background: `linear-gradient(135deg, ${themeOption.primary[500]} 0%, ${themeOption.primary[600]} 100%)` }"
                  ></div>
                  <div class="text-center">
                    <span class="block text-sm font-bold text-gray-900 dark:text-white">
                      {{ themeOption.label }}
                    </span>
                    <span class="block text-xs text-gray-500 dark:text-gray-400 mt-1">
                      {{ themeOption.description }}
                    </span>
                  </div>
                  <svg v-if="currentTheme === key" class="w-5 h-5 text-primary-600 dark:text-primary-400" fill="currentColor" viewBox="0 0 20 20">
                    <path fillRule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zm3.707-9.293a1 1 0 00-1.414-1.414L9 10.586 7.707 9.293a1 1 0 00-1.414 1.414l2 2a1 1 0 001.414 0l4-4z" clipRule="evenodd" />
                  </svg>
                </div>
              </button>
            </div>
          </div>

          <!-- Light/Dark Mode -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-4">
              Display Mode
            </label>
            <div class="grid grid-cols-3 gap-4">
              <button
                v-for="mode in ['light', 'dark', 'system']"
                :key="mode"
                @click="setColorMode(mode)"
                :class="[
                  'p-5 rounded-xl border-2 transition-all duration-200 hover:scale-105 active:scale-95',
                  preferredMode === mode
                    ? 'border-primary-500 bg-primary-50 dark:bg-primary-900/30 shadow-lg shadow-primary-500/20'
                    : 'border-gray-200 dark:border-gray-700 hover:border-gray-300 dark:hover:border-gray-600'
                ]"
              >
                <div class="flex flex-col items-center space-y-3">
                  <div class="w-12 h-12 flex items-center justify-center rounded-xl bg-gradient-to-br from-gray-100 to-gray-200 dark:from-gray-800 dark:to-gray-700">
                    <svg v-if="mode === 'light'" class="w-7 h-7 text-amber-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z" />
                    </svg>
                    <svg v-else-if="mode === 'dark'" class="w-7 h-7 text-indigo-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z" />
                    </svg>
                    <svg v-else class="w-7 h-7 text-gray-600 dark:text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" />
                    </svg>
                  </div>
                  <span class="text-sm font-bold text-gray-900 dark:text-white capitalize">
                    {{ mode }}
                  </span>
                  <svg v-if="preferredMode === mode" class="w-5 h-5 text-primary-600 dark:text-primary-400" fill="currentColor" viewBox="0 0 20 20">
                    <path fillRule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zm3.707-9.293a1 1 0 00-1.414-1.414L9 10.586 7.707 9.293a1 1 0 00-1.414 1.414l2 2a1 1 0 001.414 0l4-4z" clipRule="evenodd" />
                  </svg>
                </div>
              </button>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Display Preferences -->
      <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
        <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6 flex items-center gap-2">
          <svg class="w-6 h-6 text-primary-600 dark:text-primary-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M12 6V4m0 2a2 2 0 100 4m0-4a2 2 0 110 4m-6 8a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4m6 6v10m6-2a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4" />
          </svg>
          Display Preferences
        </h2>
        
        <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
          <!-- Time Format -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-3">
              Time Format
            </label>
            <select
              v-model="settings.timeFormat"
              class="w-full px-4 py-3 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white font-medium focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
            >
              <option value="relative">Relative (e.g., "2 hours ago")</option>
              <option value="absolute">Absolute (e.g., "2023-10-15 14:30:00")</option>
            </select>
          </div>
          
          <!-- Table Rows Per Page -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-3">
              Table Rows Per Page
            </label>
            <select
              v-model.number="settings.rowsPerPage"
              class="w-full px-4 py-3 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white font-medium focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
            >
              <option :value="25">25 rows</option>
              <option :value="50">50 rows</option>
              <option :value="100">100 rows</option>
              <option :value="250">250 rows</option>
            </select>
          </div>
        </div>
      </div>
      
      <!-- API Configuration -->
      <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
        <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6 flex items-center gap-2">
          <svg class="w-6 h-6 text-primary-600 dark:text-primary-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M10 20l4-16m4 4l4 4-4 4M6 16l-4-4 4-4" />
          </svg>
          API Configuration
        </h2>
        
        <div class="space-y-6">
          <!-- API Base URL -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-3">
              API Base URL
            </label>
            <input
              v-model="settings.apiBaseUrl"
              type="text"
              placeholder="http://localhost:6632"
              class="w-full px-4 py-3 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white font-medium focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
            />
            <p class="mt-2 text-sm text-gray-500 dark:text-gray-400">
              Current: <span class="font-semibold">http://localhost:6632</span>
            </p>
          </div>
          
          <!-- Connection Status -->
          <div class="p-5 bg-gradient-to-br from-emerald-50 to-teal-50/50 dark:from-emerald-900/20 dark:to-teal-900/10 rounded-xl border border-emerald-200 dark:border-emerald-800/30">
            <div class="flex items-center gap-4">
              <div class="flex-shrink-0">
                <div class="w-12 h-12 bg-emerald-500 rounded-xl flex items-center justify-center shadow-lg shadow-emerald-500/30">
                  <svg class="w-6 h-6 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M5 13l4 4L19 7" />
                  </svg>
                </div>
              </div>
              <div class="flex-1">
                <div class="text-base font-black text-emerald-900 dark:text-emerald-100">
                  Connected
                </div>
                <div class="text-sm text-emerald-700 dark:text-emerald-300 mt-1">
                  API server is responding normally
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Actions -->
      <div class="flex items-center justify-between gap-4">
        <button
          @click="resetSettings"
          class="px-6 py-3 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 text-gray-900 dark:text-white rounded-xl font-semibold"
        >
          Reset to Defaults
        </button>
        
        <button
          @click="saveSettings"
          class="px-8 py-3 bg-gradient-to-r from-primary-600 to-primary-700 hover:from-primary-700 hover:to-primary-800 text-white rounded-xl font-semibold"
        >
          Save Settings
        </button>
      </div>
      
      <!-- Success Message -->
      <div 
        v-if="showSuccess"
        class="p-5 bg-gradient-to-br from-emerald-50 to-teal-50/50 dark:from-emerald-900/20 dark:to-teal-900/10 rounded-xl border border-emerald-200 dark:border-emerald-800/30 shadow-lg"
      >
        <div class="flex items-center gap-3">
          <div class="flex-shrink-0 w-10 h-10 bg-emerald-500 rounded-xl flex items-center justify-center shadow-lg shadow-emerald-500/30">
            <svg class="w-5 h-5 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M5 13l4 4L19 7" />
            </svg>
          </div>
          <span class="text-sm font-bold text-emerald-900 dark:text-emerald-100">
            Settings saved successfully!
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
  apiBaseUrl: 'http://localhost:6632',
  timeFormat: 'relative',
  rowsPerPage: 50
});

const showSuccess = ref(false);

const defaultSettings = {
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

