<template>
  <div v-if="loading" class="flex items-center justify-center py-16">
    <div class="text-center">
      <div class="relative inline-block">
        <div class="animate-spin rounded-full h-14 w-14 border-4 border-gray-200 dark:border-gray-800"></div>
        <div class="absolute inset-0 animate-spin rounded-full h-14 w-14 border-4 border-transparent border-t-emerald-600 dark:border-t-emerald-400" style="animation-duration: 0.8s"></div>
      </div>
      <p class="mt-5 text-sm font-medium text-gray-600 dark:text-gray-400 animate-pulse">
        {{ message || 'Loading...' }}
      </p>
    </div>
  </div>
  
  <div v-else-if="error" class="rounded-xl bg-red-50 dark:bg-red-900/20 border-2 border-red-200 dark:border-red-800 p-6 shadow-sm">
    <div class="flex">
      <div class="flex-shrink-0">
        <div class="p-2 bg-red-100 dark:bg-red-900/30 rounded-lg">
          <svg class="h-6 w-6 text-red-600 dark:text-red-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
        </div>
      </div>
      <div class="ml-4 flex-1">
        <h3 class="text-base font-semibold text-red-900 dark:text-red-200">
          Error loading data
        </h3>
        <div class="mt-2 text-sm text-red-700 dark:text-red-300">
          <p>{{ error }}</p>
        </div>
        <div v-if="retry" class="mt-4">
          <button
            @click="$emit('retry')"
            class="inline-flex items-center px-4 py-2 bg-red-600 dark:bg-red-700 hover:bg-red-700 dark:hover:bg-red-600 text-white text-sm font-medium rounded-lg transition-colors"
          >
            <svg class="w-4 h-4 mr-2" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
            Try again
          </button>
        </div>
      </div>
    </div>
  </div>
  
  <div v-else-if="empty" class="text-center py-16">
    <div class="inline-flex p-4 bg-gray-100 dark:bg-gray-800 rounded-2xl">
      <svg class="h-16 w-16 text-gray-400 dark:text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
      </svg>
    </div>
    <h3 class="mt-6 text-base font-semibold text-gray-900 dark:text-gray-100">
      {{ emptyMessage || 'No data available' }}
    </h3>
    <p class="mt-2 text-sm text-gray-600 dark:text-gray-400 max-w-md mx-auto">
      {{ emptyDescription || 'There is no data to display at this time.' }}
    </p>
  </div>
  
  <slot v-else />
</template>

<script setup>
defineProps({
  loading: {
    type: Boolean,
    default: false
  },
  error: {
    type: String,
    default: null
  },
  empty: {
    type: Boolean,
    default: false
  },
  message: {
    type: String,
    default: ''
  },
  emptyMessage: {
    type: String,
    default: ''
  },
  emptyDescription: {
    type: String,
    default: ''
  },
  retry: {
    type: Boolean,
    default: true
  }
});

defineEmits(['retry']);
</script>

