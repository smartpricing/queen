<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Stats cards -->
    <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-5 gap-2 sm:gap-4">
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Total Queues</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-light-900 dark:text-white mt-1">
          {{ formatNumber(filteredQueues.length) }}
        </p>
        <p class="text-[10px] sm:text-xs text-light-500 mt-1">{{ formatNumber(totalPartitions) }} partitions</p>
      </div>
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Total Messages</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-cyber-600 dark:text-cyber-400 mt-1">
          {{ formatNumber(totalMessages) }}
        </p>
      </div>
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Pending</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-queen-600 dark:text-queen-400 mt-1">
          {{ formatNumber(totalPending) }}
        </p>
      </div>
      <div class="card p-3 sm:p-4">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Processing</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-crown-600 dark:text-crown-400 mt-1">
          {{ formatNumber(totalProcessing) }}
        </p>
      </div>
      <div class="card p-3 sm:p-4 col-span-2 sm:col-span-1">
        <p class="text-[10px] sm:text-xs text-light-500 uppercase tracking-wide">Active Queues</p>
        <p class="text-xl sm:text-2xl font-bold font-display text-emerald-600 dark:text-emerald-400 mt-1">
          {{ formatNumber(activeQueues) }}
        </p>
      </div>
    </div>

    <!-- Filters -->
    <div class="card p-3 sm:p-4">
      <div class="flex flex-col sm:flex-row sm:flex-wrap sm:items-center gap-3 sm:gap-4">
        <div class="w-full sm:flex-1 sm:min-w-[200px] sm:max-w-md">
          <div class="relative">
            <input
              v-model="searchQuery"
              type="text"
              placeholder="Search queues..."
              class="input pl-10"
            />
            <svg 
              class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-light-500"
              fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
            >
              <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
            </svg>
          </div>
        </div>
        
        <select v-model="filterNamespace" class="select w-full sm:w-40">
          <option value="">All Namespaces</option>
          <option v-for="ns in namespaces" :key="ns" :value="ns">
            {{ ns || '(empty)' }}
          </option>
        </select>
        
        <select v-model="filterTask" class="select w-full sm:w-40">
          <option value="">All Tasks</option>
          <option v-for="task in tasks" :key="task" :value="task">
            {{ task || '(empty)' }}
          </option>
        </select>
        
        <select v-model="sortBy" class="select w-full sm:w-40">
          <option value="name">Sort by Name</option>
          <option value="pending">Sort by Pending</option>
          <option value="total">Sort by Total</option>
        </select>
      </div>
    </div>

    <!-- Queues Table -->
    <div class="card overflow-hidden">
      <div class="overflow-x-auto">
        <table class="w-full">
          <thead>
            <tr class="border-b border-light-200 dark:border-dark-50">
              <th class="text-left px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Queue Name
              </th>
              <th class="text-right px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Partitions
              </th>
              <th class="text-right px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Pending
              </th>
              <th class="text-right px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Total
              </th>
              <th class="text-center px-4 py-3 text-xs font-semibold text-light-500 uppercase tracking-wide">
                Actions
              </th>
            </tr>
          </thead>
          <tbody v-if="loading">
            <tr v-for="i in 5" :key="i" class="border-b border-light-100 dark:border-dark-100">
              <td class="px-4 py-3"><div class="skeleton h-4 w-32" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-8 ml-auto" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-16 ml-auto" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-16 ml-auto" /></td>
              <td class="px-4 py-3"><div class="skeleton h-4 w-16 mx-auto" /></td>
            </tr>
          </tbody>
          <tbody v-else-if="filteredQueues.length > 0">
            <tr 
              v-for="queue in filteredQueues" 
              :key="queue.name"
              class="border-b border-light-100 dark:border-dark-100 hover:bg-light-50 dark:hover:bg-dark-300 cursor-pointer transition-colors"
              @click="$router.push(`/queues/${queue.name}`)"
            >
              <td class="px-4 py-3">
                <span class="font-medium text-light-900 dark:text-white hover:text-queen-600 dark:hover:text-queen-400 transition-colors">
                  {{ queue.name }}
                </span>
              </td>
              <td class="px-4 py-3 text-right text-sm text-light-700 dark:text-light-300">
                {{ queue.partitions || 1 }}
              </td>
              <td class="px-4 py-3 text-right">
                <span class="text-sm font-medium text-cyber-600 dark:text-cyber-400">
                  {{ formatNumber(queue.messages?.pending || 0) }}
                </span>
              </td>
              <td class="px-4 py-3 text-right">
                <span class="text-sm font-semibold text-light-900 dark:text-white">
                  {{ formatNumber(queue.messages?.total || 0) }}
                </span>
              </td>
              <td class="px-4 py-3">
                <div class="flex items-center justify-center gap-1">
                  <button 
                    @click.stop="viewQueue(queue)"
                    class="p-1.5 rounded hover:bg-light-200 dark:hover:bg-dark-200 text-light-600 dark:text-light-400 hover:text-queen-600 dark:hover:text-queen-400 transition-colors"
                    title="View details"
                  >
                    <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M2.036 12.322a1.012 1.012 0 010-.639C3.423 7.51 7.36 4.5 12 4.5c4.638 0 8.573 3.007 9.963 7.178.07.207.07.431 0 .639C20.577 16.49 16.64 19.5 12 19.5c-4.638 0-8.573-3.007-9.963-7.178z" />
                      <path stroke-linecap="round" stroke-linejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
                    </svg>
                  </button>
                  <button 
                    @click.stop="confirmDelete(queue)"
                    class="p-1.5 rounded hover:bg-rose-100 dark:hover:bg-rose-900/30 text-light-600 dark:text-light-400 hover:text-rose-600 dark:hover:text-rose-400 transition-colors"
                    title="Delete queue"
                  >
                    <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                      <path stroke-linecap="round" stroke-linejoin="round" d="M14.74 9l-.346 9m-4.788 0L9.26 9m9.968-3.21c.342.052.682.107 1.022.166m-1.022-.165L18.16 19.673a2.25 2.25 0 01-2.244 2.077H8.084a2.25 2.25 0 01-2.244-2.077L4.772 5.79m14.456 0a48.108 48.108 0 00-3.478-.397m-12 .562c.34-.059.68-.114 1.022-.165m0 0a48.11 48.11 0 013.478-.397m7.5 0v-.916c0-1.18-.91-2.164-2.09-2.201a51.964 51.964 0 00-3.32 0c-1.18.037-2.09 1.022-2.09 2.201v.916m7.5 0a48.667 48.667 0 00-7.5 0" />
                    </svg>
                  </button>
                </div>
              </td>
            </tr>
          </tbody>
          <tbody v-else>
            <tr>
              <td colspan="5" class="px-4 py-12 text-center">
                <svg class="w-12 h-12 mx-auto text-light-400 dark:text-light-600 mb-3" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M3.75 12h16.5m-16.5 3.75h16.5M3.75 19.5h16.5M5.625 4.5h12.75a1.875 1.875 0 010 3.75H5.625a1.875 1.875 0 010-3.75z" />
                </svg>
                <h3 class="text-sm font-semibold text-light-900 dark:text-white mb-1">No queues found</h3>
                <p class="text-sm text-light-600 dark:text-light-400">
                  {{ searchQuery || filterNamespace || filterTask ? 'Try adjusting your filters' : 'Create a queue to get started' }}
                </p>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- Delete confirmation modal -->
    <div 
      v-if="showDeleteModal"
      class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-dark-500/50 backdrop-blur-sm"
      @click="showDeleteModal = false"
    >
      <div 
        class="card p-6 w-full max-w-md animate-slide-up"
        @click.stop
      >
        <h3 class="text-lg font-semibold text-light-900 dark:text-white mb-2">
          Delete Queue
        </h3>
        <p class="text-light-600 dark:text-light-400 mb-6">
          Are you sure you want to delete <strong>{{ queueToDelete?.name }}</strong>? This will permanently remove the queue and all its messages. This action cannot be undone.
        </p>
        <div class="flex items-center justify-end gap-3">
          <button @click="showDeleteModal = false" class="btn btn-secondary">
            Cancel
          </button>
          <button @click="deleteQueue" class="btn bg-rose-600 text-white hover:bg-rose-700">
            Delete Queue
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { queues as queuesApi } from '@/api'
import { formatNumber } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'

const router = useRouter()

// State
const queues = ref([])
const loading = ref(true)
const searchQuery = ref('')
const filterNamespace = ref('')
const filterTask = ref('')
const sortBy = ref('name')

// Modal state
const showDeleteModal = ref(false)
const queueToDelete = ref(null)

// Get unique namespaces and tasks from queues
const namespaces = computed(() => {
  const set = new Set(queues.value.map(q => q.namespace).filter(ns => ns !== undefined))
  return [...set].sort()
})

const tasks = computed(() => {
  const set = new Set(queues.value.map(q => q.task).filter(t => t !== undefined))
  return [...set].sort()
})

// Computed
const filteredQueues = computed(() => {
  let result = [...queues.value]
  
  // Search filter
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase()
    result = result.filter(q => q.name.toLowerCase().includes(query))
  }
  
  // Namespace filter
  if (filterNamespace.value !== '') {
    result = result.filter(q => q.namespace === filterNamespace.value)
  }
  
  // Task filter
  if (filterTask.value !== '') {
    result = result.filter(q => q.task === filterTask.value)
  }
  
  // Sort
  result.sort((a, b) => {
    if (sortBy.value === 'name') {
      return a.name.localeCompare(b.name)
    }
    // For pending/total, look inside messages object
    const aVal = a.messages?.[sortBy.value] || 0
    const bVal = b.messages?.[sortBy.value] || 0
    return bVal - aVal
  })
  
  return result
})

const totalMessages = computed(() => 
  filteredQueues.value.reduce((sum, q) => sum + (q.messages?.total || 0), 0)
)

const totalPending = computed(() =>
  filteredQueues.value.reduce((sum, q) => sum + (q.messages?.pending || 0), 0)
)

const totalProcessing = computed(() =>
  filteredQueues.value.reduce((sum, q) => sum + (q.messages?.processing || 0), 0)
)

const totalPartitions = computed(() =>
  filteredQueues.value.reduce((sum, q) => sum + (q.partitions || 1), 0)
)

const activeQueues = computed(() =>
  filteredQueues.value.filter(q => (q.messages?.processing || 0) > 0).length
)

// Methods
const fetchQueues = async () => {
  loading.value = true
  try {
    const response = await queuesApi.list()
    const allQueues = response.data?.queues || response.data || []
    // Filter out queues with empty or null names
    queues.value = allQueues.filter(q => q.name && q.name.trim() !== '')
  } catch (err) {
    console.error('Failed to fetch queues:', err)
  } finally {
    loading.value = false
  }
}

const viewQueue = (queue) => {
  router.push(`/queues/${queue.name}`)
}

const confirmDelete = (queue) => {
  queueToDelete.value = queue
  showDeleteModal.value = true
}

const deleteQueue = async () => {
  if (!queueToDelete.value) return
  
  try {
    await queuesApi.delete(queueToDelete.value.name)
    showDeleteModal.value = false
    queueToDelete.value = null
    fetchQueues()
  } catch (err) {
    console.error('Failed to delete queue:', err)
  }
}

// Register for global refresh
useRefresh(fetchQueues)

onMounted(fetchQueues)
</script>
