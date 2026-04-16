<template>
  <div class="card">
    <div class="card-header" v-if="title">
      <h3>{{ title }}</h3>
      <span v-if="subtitle" class="muted">{{ subtitle }}</span>
    </div>

    <div v-if="loading" class="p-4 space-y-3">
      <div v-for="i in pageSize" :key="i" class="skeleton" style="height:40px; border-radius:6px;" />
    </div>

    <table v-else class="t">
      <thead>
        <tr>
          <th
            v-for="col in columns"
            :key="col.key"
            :style="col.align === 'right' ? 'text-align:right' : ''"
            :class="{ 'cursor-pointer': col.sortable }"
            @click="col.sortable && toggleSort(col.key)"
          >
            {{ col.label }}
            <span v-if="col.sortable && sortKey === col.key" style="margin-left:4px;">
              {{ sortDir === 'asc' ? '↑' : '↓' }}
            </span>
          </th>
        </tr>
      </thead>
      <tbody>
        <tr
          v-for="(row, idx) in paginatedData"
          :key="idx"
          :class="{ 'cursor-pointer': clickable }"
          @click="clickable && $emit('row-click', row)"
        >
          <td
            v-for="col in columns"
            :key="col.key"
            :style="col.align === 'right' ? 'text-align:right' : ''"
          >
            <slot :name="col.key" :value="getNestedValue(row, col.key)" :row="row">
              <span class="font-mono text-[12px]">{{ getNestedValue(row, col.key) ?? '-' }}</span>
            </slot>
          </td>
        </tr>
        <tr v-if="paginatedData.length === 0">
          <td :colspan="columns.length" style="text-align:center; color:var(--text-low); padding:24px;">
            No data
          </td>
        </tr>
      </tbody>
    </table>

    <!-- Pagination -->
    <div v-if="totalPages > 1" style="display:flex; align-items:center; justify-content:space-between; padding:10px 16px; border-top:1px solid var(--bd); font-size:12px; color:var(--text-mid);">
      <span>{{ (currentPage - 1) * pageSize + 1 }}–{{ Math.min(currentPage * pageSize, sortedData.length) }} of {{ sortedData.length }}</span>
      <div style="display:flex; gap:4px;">
        <button class="btn btn-ghost btn-icon" :disabled="currentPage <= 1" @click="currentPage--" style="padding:4px;">
          <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M15 6l-6 6 6 6"/></svg>
        </button>
        <button class="btn btn-ghost btn-icon" :disabled="currentPage >= totalPages" @click="currentPage++" style="padding:4px;">
          <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M9 6l6 6-6 6"/></svg>
        </button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'

const props = defineProps({
  title: { type: String, default: '' },
  subtitle: { type: String, default: '' },
  columns: { type: Array, required: true },
  data: { type: Array, default: () => [] },
  loading: { type: Boolean, default: false },
  pageSize: { type: Number, default: 10 },
  clickable: { type: Boolean, default: false },
})

defineEmits(['row-click'])

const sortKey = ref(null)
const sortDir = ref('asc')
const currentPage = ref(1)

watch(() => props.data, () => { currentPage.value = 1 })

const toggleSort = (key) => {
  if (sortKey.value === key) { sortDir.value = sortDir.value === 'asc' ? 'desc' : 'asc' }
  else { sortKey.value = key; sortDir.value = 'asc' }
}

const getNestedValue = (obj, path) => {
  return path.split('.').reduce((o, k) => o?.[k], obj)
}

const sortedData = computed(() => {
  if (!sortKey.value) return props.data
  return [...props.data].sort((a, b) => {
    const va = getNestedValue(a, sortKey.value)
    const vb = getNestedValue(b, sortKey.value)
    const cmp = typeof va === 'number' ? va - vb : String(va || '').localeCompare(String(vb || ''))
    return sortDir.value === 'asc' ? cmp : -cmp
  })
})

const totalPages = computed(() => Math.ceil(sortedData.value.length / props.pageSize))

const paginatedData = computed(() => {
  const start = (currentPage.value - 1) * props.pageSize
  return sortedData.value.slice(start, start + props.pageSize)
})
</script>
