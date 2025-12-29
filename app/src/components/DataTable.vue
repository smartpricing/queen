<template>
  <div class="card overflow-hidden">
    <!-- Header -->
    <div v-if="title || $slots.header" class="card-header flex items-center justify-between">
      <div class="flex items-center gap-3">
        <div>
          <h3 v-if="title" class="font-semibold text-light-900 dark:text-white">
            {{ title }}
          </h3>
          <p v-if="subtitle" class="text-xs text-light-500 mt-0.5">{{ subtitle }}</p>
        </div>
        <span v-if="total" class="badge badge-queen">
          {{ formatNumber(total) }} total
        </span>
      </div>
      <slot name="header" />
    </div>
    
    <!-- Table -->
    <div class="table-container">
      <table class="table">
        <thead>
          <tr>
            <th 
              v-for="column in columns" 
              :key="column.key"
              :class="[
                column.align === 'right' ? 'text-right' : '',
                column.sortable ? 'cursor-pointer hover:text-queen-600 dark:hover:text-queen-400 select-none' : ''
              ]"
              @click="column.sortable && handleSort(column.key)"
            >
              <div class="flex items-center gap-1.5" :class="{ 'justify-end': column.align === 'right' }">
                {{ column.label }}
                <svg 
                  v-if="column.sortable && sortKey === column.key"
                  class="w-3.5 h-3.5 transition-transform"
                  :class="{ 'rotate-180': sortOrder === 'desc' }"
                  fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2"
                >
                  <path stroke-linecap="round" stroke-linejoin="round" d="M5 15l7-7 7 7" />
                </svg>
              </div>
            </th>
          </tr>
        </thead>
        <tbody>
          <template v-if="loading">
            <tr v-for="i in 5" :key="i">
              <td v-for="column in columns" :key="column.key">
                <div class="skeleton h-4 w-24" />
              </td>
            </tr>
          </template>
          <template v-else-if="sortedData.length > 0">
            <tr 
              v-for="(row, index) in paginatedData" 
              :key="row.id || index"
              class="transition-colors"
              :class="{ 'cursor-pointer': clickable }"
              @click="clickable && $emit('row-click', row)"
            >
              <td 
                v-for="column in columns" 
                :key="column.key"
                :class="column.align === 'right' ? 'text-right' : ''"
              >
                <slot :name="column.key" :row="row" :value="getValue(row, column.key)">
                  {{ getValue(row, column.key) }}
                </slot>
              </td>
            </tr>
          </template>
          <tr v-else>
            <td :colspan="columns.length" class="text-center py-12">
              <div class="text-light-500 dark:text-light-500">
                <svg class="w-12 h-12 mx-auto mb-3 opacity-50" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                </svg>
                <p class="text-sm">{{ emptyText }}</p>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
    
    <!-- Pagination -->
    <div v-if="pagination && sortedData.length > pageSize" class="px-5 py-4 border-t border-light-200 dark:border-dark-50 flex items-center justify-between">
      <p class="text-sm text-light-600 dark:text-light-400">
        Showing {{ (currentPage - 1) * pageSize + 1 }} to {{ Math.min(currentPage * pageSize, sortedData.length) }} of {{ sortedData.length }}
      </p>
      <div class="flex items-center gap-2">
        <button 
          @click="currentPage--"
          :disabled="currentPage === 1"
          class="btn btn-secondary px-3 py-1.5 text-sm disabled:opacity-50"
        >
          Previous
        </button>
        <button 
          @click="currentPage++"
          :disabled="currentPage >= totalPages"
          class="btn btn-secondary px-3 py-1.5 text-sm disabled:opacity-50"
        >
          Next
        </button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { formatNumber } from '@/composables/useApi'

const props = defineProps({
  columns: { type: Array, required: true },
  data: { type: Array, default: () => [] },
  title: { type: String, default: null },
  subtitle: { type: String, default: null },
  total: { type: Number, default: null },
  loading: { type: Boolean, default: false },
  clickable: { type: Boolean, default: false },
  pagination: { type: Boolean, default: true },
  pageSize: { type: Number, default: 10 },
  emptyText: { type: String, default: 'No data available' },
  defaultSort: { type: String, default: null },
  defaultSortOrder: { type: String, default: 'asc' },
})

defineEmits(['row-click'])

const currentPage = ref(1)
const sortKey = ref(props.defaultSort)
const sortOrder = ref(props.defaultSortOrder)

const getValue = (row, key) => {
  return key.split('.').reduce((obj, k) => obj?.[k], row)
}

const sortedData = computed(() => {
  if (!sortKey.value) return props.data
  
  return [...props.data].sort((a, b) => {
    const aVal = getValue(a, sortKey.value)
    const bVal = getValue(b, sortKey.value)
    
    if (aVal === bVal) return 0
    
    const comparison = aVal < bVal ? -1 : 1
    return sortOrder.value === 'asc' ? comparison : -comparison
  })
})

const totalPages = computed(() => Math.ceil(sortedData.value.length / props.pageSize))

const paginatedData = computed(() => {
  if (!props.pagination) return sortedData.value
  
  const start = (currentPage.value - 1) * props.pageSize
  return sortedData.value.slice(start, start + props.pageSize)
})

const handleSort = (key) => {
  if (sortKey.value === key) {
    sortOrder.value = sortOrder.value === 'asc' ? 'desc' : 'asc'
  } else {
    sortKey.value = key
    sortOrder.value = 'asc'
  }
  currentPage.value = 1
}
</script>

