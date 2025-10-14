<template>
  <div class="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 overflow-hidden">
    <!-- Search and Actions -->
    <div class="p-4 border-b border-gray-200 dark:border-gray-800 flex items-center justify-between gap-4">
      <div class="relative flex-1 max-w-md">
        <input
          :value="search"
          @input="$emit('update:search', $event.target.value)"
          type="text"
          :placeholder="searchPlaceholder"
          class="w-full pl-10 pr-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white text-sm focus:ring-2 focus:ring-primary-500 focus:border-transparent"
        />
        <svg class="absolute left-3 top-2.5 w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
        </svg>
      </div>
      
      <div class="flex items-center gap-2">
        <span v-if="totalItems" class="text-sm text-gray-600 dark:text-gray-400">
          {{ startIndex }}-{{ endIndex }} of {{ totalItems }}
        </span>
        <button
          @click="$emit('prev-page')"
          :disabled="!canGoPrev"
          class="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-800 disabled:opacity-30 disabled:cursor-not-allowed"
        >
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M15 19l-7-7 7-7" />
          </svg>
        </button>
        <button
          @click="$emit('next-page')"
          :disabled="!canGoNext"
          class="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-800 disabled:opacity-30 disabled:cursor-not-allowed"
        >
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M9 5l7 7-7 7" />
          </svg>
        </button>
      </div>
    </div>
    
    <!-- Table -->
    <div class="overflow-x-auto">
      <table class="w-full">
        <thead class="bg-gray-50 dark:bg-gray-800/50">
          <tr>
            <th
              v-for="column in columns"
              :key="column.key"
              :class="[
                'px-6 py-3 text-left text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider',
                column.sortable && 'cursor-pointer hover:text-gray-900 dark:hover:text-gray-200'
              ]"
              @click="column.sortable && $emit('sort', column.key)"
            >
              <div class="flex items-center gap-2">
                <span>{{ column.label }}</span>
                <svg v-if="column.sortable" class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M8 9l4-4 4 4m0 6l-4 4-4-4" />
                </svg>
              </div>
            </th>
          </tr>
        </thead>
        <tbody class="divide-y divide-gray-200 dark:divide-gray-800">
          <slot />
        </tbody>
      </table>
    </div>
    
    <!-- Empty State -->
    <div v-if="!hasData" class="text-center py-12">
      <svg class="w-12 h-12 mx-auto text-gray-300 dark:text-gray-700 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
      </svg>
      <p class="text-sm font-medium text-gray-500 dark:text-gray-400">{{ emptyMessage }}</p>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  columns: {
    type: Array,
    required: true
  },
  search: {
    type: String,
    default: ''
  },
  searchPlaceholder: {
    type: String,
    default: 'Search...'
  },
  currentPage: {
    type: Number,
    default: 1
  },
  pageSize: {
    type: Number,
    default: 25
  },
  totalItems: {
    type: Number,
    default: 0
  },
  hasData: {
    type: Boolean,
    default: true
  },
  emptyMessage: {
    type: String,
    default: 'No data available'
  }
});

defineEmits(['update:search', 'sort', 'prev-page', 'next-page']);

const startIndex = computed(() => (props.currentPage - 1) * props.pageSize + 1);
const endIndex = computed(() => Math.min(props.currentPage * props.pageSize, props.totalItems));
const canGoPrev = computed(() => props.currentPage > 1);
const canGoNext = computed(() => props.currentPage * props.pageSize < props.totalItems);
</script>

