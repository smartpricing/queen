<template>
  <div v-if="loading" class="flex items-center justify-center py-12">
    <div class="text-center">
      <div class="inline-block animate-spin rounded-full h-12 w-12 border-4 border-gray-300 dark:border-gray-700 border-t-green-600 dark:border-t-green-400"></div>
      <p class="mt-4 text-sm text-gray-600 dark:text-gray-400">
        {{ message || 'Loading...' }}
      </p>
    </div>
  </div>
  
  <div v-else-if="error" class="rounded-lg bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 p-4">
    <div class="flex">
      <div class="flex-shrink-0">
        <svg class="h-5 w-5 text-red-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
        </svg>
      </div>
      <div class="ml-3">
        <h3 class="text-sm font-medium text-red-800 dark:text-red-300">
          Error loading data
        </h3>
        <div class="mt-2 text-sm text-red-700 dark:text-red-400">
          <p>{{ error }}</p>
        </div>
        <div v-if="retry" class="mt-4">
          <button
            @click="$emit('retry')"
            class="text-sm font-medium text-red-800 dark:text-red-300 hover:text-red-700 dark:hover:text-red-200"
          >
            Try again
          </button>
        </div>
      </div>
    </div>
  </div>
  
  <div v-else-if="empty" class="text-center py-12">
    <svg class="mx-auto h-12 w-12 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
    </svg>
    <h3 class="mt-2 text-sm font-medium text-gray-900 dark:text-gray-100">
      {{ emptyMessage || 'No data available' }}
    </h3>
    <p class="mt-1 text-sm text-gray-500 dark:text-gray-400">
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

