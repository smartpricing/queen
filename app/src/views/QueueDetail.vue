<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Loading state -->
    <div v-if="loading" class="space-y-4 sm:space-y-6">
      <div class="card p-5">
        <div class="skeleton h-8 w-48 mb-4" />
        <div class="skeleton h-4 w-64" />
      </div>
      <div class="grid grid-cols-2 md:grid-cols-4 gap-2 sm:gap-4">
        <div v-for="i in 4" :key="i" class="card p-4">
          <div class="skeleton h-4 w-20 mb-2" />
          <div class="skeleton h-8 w-16" />
        </div>
      </div>
    </div>
    
    <template v-else-if="queueData || statusData">
      <!-- Header Card -->
      <div class="card p-5">
        <div class="flex items-center justify-between mb-3">
          <div class="flex items-center gap-3">
            <button @click="$router.push('/queues')" class="p-2 hover:bg-light-100 dark:hover:bg-dark-300 rounded-lg transition-colors">
              <svg class="w-5 h-5 text-light-600 dark:text-light-400" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M10 19l-7-7m0 0l7-7m-7 7h18" />
              </svg>
            </button>
            <h1 class="text-xl font-bold font-display text-light-900 dark:text-white">
              {{ queueData?.name || queueName }}
            </h1>
          </div>
          
          <button @click="showDeleteModal = true" class="btn bg-rose-600 text-white hover:bg-rose-700">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
            </svg>
            Delete Queue
          </button>
        </div>
        
        <div class="flex flex-wrap gap-x-6 gap-y-1 text-sm text-light-600 dark:text-light-400">
          <span v-if="queueData?.namespace">
            <span class="text-light-500">Namespace:</span> {{ queueData.namespace }}
          </span>
          <span v-if="queueData?.task">
            <span class="text-light-500">Task:</span> {{ queueData.task }}
          </span>
          <span>
            <span class="text-light-500">Partitions:</span> {{ partitions.length }}
          </span>
          <span class="text-xs text-light-500">
            Created: {{ formatDate(queueData?.createdAt) }}
          </span>
        </div>
      </div>

      <!-- Metrics Grid -->
      <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-5 gap-2 sm:gap-4">
        <div class="card p-4">
          <p class="text-xs text-light-500 uppercase tracking-wide">Total</p>
          <p class="text-2xl font-bold font-display text-light-900 dark:text-white mt-1">
            {{ formatNumber(totalMessages.total) }}
          </p>
        </div>
        <div class="card p-4">
          <p class="text-xs text-light-500 uppercase tracking-wide">Pending</p>
          <p class="text-2xl font-bold font-display text-cyber-600 dark:text-cyber-400 mt-1">
            {{ formatNumber(totalMessages.pending) }}
          </p>
        </div>
        <div class="card p-4">
          <p class="text-xs text-light-500 uppercase tracking-wide">Processing</p>
          <p class="text-2xl font-bold font-display text-crown-600 dark:text-crown-400 mt-1">
            {{ formatNumber(totalMessages.processing) }}
          </p>
        </div>
        <div class="card p-4">
          <p class="text-xs text-light-500 uppercase tracking-wide">Completed</p>
          <p class="text-2xl font-bold font-display text-emerald-600 dark:text-emerald-400 mt-1">
            {{ formatNumber(totalMessages.completed) }}
          </p>
        </div>
        <div class="card p-4">
          <p class="text-xs text-light-500 uppercase tracking-wide">Failed / DLQ</p>
          <p class="text-2xl font-bold font-display text-rose-600 dark:text-rose-400 mt-1">
            {{ formatNumber(totalMessages.failed + totalMessages.deadLetter) }}
          </p>
        </div>
      </div>

      <!-- Queue Configuration -->
      <div v-if="queueData?.config" class="card p-5">
        <h3 class="text-sm font-semibold text-light-900 dark:text-white mb-4">Queue Configuration</h3>
        <div class="grid grid-cols-2 md:grid-cols-3 gap-x-3 sm:gap-x-6 gap-y-2 sm:gap-y-3 text-xs sm:text-sm">
          <div>
            <span class="text-light-500 block mb-0.5">Lease Time</span>
            <span class="font-semibold">{{ queueData.config.leaseTime || 0 }}s</span>
          </div>
          <div>
            <span class="text-light-500 block mb-0.5">TTL</span>
            <span class="font-semibold">{{ formatDuration(queueData.config.ttl || 0) }}</span>
          </div>
          <div>
            <span class="text-light-500 block mb-0.5">Max Queue Size</span>
            <span class="font-semibold">{{ formatNumber(queueData.config.maxQueueSize || 0) }}</span>
          </div>
          <div>
            <span class="text-light-500 block mb-0.5">Retry Limit</span>
            <span class="font-semibold">{{ queueData.config.retryLimit || 0 }}</span>
          </div>
          <div>
            <span class="text-light-500 block mb-0.5">Retry Delay</span>
            <span class="font-semibold">{{ queueData.config.retryDelay || 0 }}ms</span>
          </div>
          <div>
            <span class="text-light-500 block mb-0.5">DLQ Enabled</span>
            <span class="font-semibold">{{ queueData.config.deadLetterQueue ? 'Yes' : 'No' }}</span>
          </div>
        </div>
      </div>

      <!-- Partitions Table -->
      <div class="card">
        <div class="card-header flex items-center justify-between">
          <div class="flex items-center gap-3">
            <h3 class="font-semibold text-light-900 dark:text-white">Partitions</h3>
            <span class="badge badge-queen">{{ partitions.length }} partitions</span>
          </div>
          <div class="w-48">
            <input
              v-model="partitionSearch"
              type="text"
              placeholder="Search partitions..."
              class="input text-sm"
            />
          </div>
        </div>
        
        <div class="overflow-x-auto">
          <table class="table">
            <thead>
              <tr>
                <th @click="sortPartitions('name')" class="cursor-pointer hover:text-queen-500 transition-colors">
                  <div class="flex items-center gap-1">
                    Partition
                    <svg class="w-3 h-3 transition-transform" :class="getSortClass('name')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('pending')" class="text-right cursor-pointer hover:text-queen-500 transition-colors">
                  <div class="flex items-center justify-end gap-1">
                    Pending
                    <svg class="w-3 h-3 transition-transform" :class="getSortClass('pending')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('processing')" class="text-right cursor-pointer hover:text-queen-500 transition-colors">
                  <div class="flex items-center justify-end gap-1">
                    Processing
                    <svg class="w-3 h-3 transition-transform" :class="getSortClass('processing')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('completed')" class="text-right cursor-pointer hover:text-queen-500 transition-colors">
                  <div class="flex items-center justify-end gap-1">
                    Completed
                    <svg class="w-3 h-3 transition-transform" :class="getSortClass('completed')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('failed')" class="text-right cursor-pointer hover:text-queen-500 transition-colors">
                  <div class="flex items-center justify-end gap-1">
                    Failed
                    <svg class="w-3 h-3 transition-transform" :class="getSortClass('failed')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th class="text-right">Consumed</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="partition in sortedPartitions" :key="partition.id" class="hover:bg-light-50 dark:hover:bg-dark-300">
                <td>
                  <div class="font-medium text-light-900 dark:text-light-100">{{ partition.name }}</div>
                  <div v-if="partition.lastActivity" class="text-xs text-light-500 mt-0.5">
                    Last: {{ formatTime(partition.lastActivity) }}
                  </div>
                </td>
                <td class="text-right font-medium tabular-nums">{{ formatNumber(partition.messages?.pending || 0) }}</td>
                <td class="text-right font-medium tabular-nums">{{ formatNumber(partition.messages?.processing || 0) }}</td>
                <td class="text-right font-medium tabular-nums text-emerald-600 dark:text-emerald-400">
                  {{ formatNumber(partition.messages?.completed || 0) }}
                </td>
                <td class="text-right font-medium tabular-nums text-rose-600 dark:text-rose-400">
                  {{ formatNumber(partition.messages?.failed || 0) }}
                </td>
                <td class="text-right">
                  <div class="text-sm tabular-nums">{{ formatNumber(partition.cursor?.totalConsumed || 0) }}</div>
                  <div class="text-xs text-light-500">
                    {{ partition.cursor?.batchesConsumed || 0 }} batches
                  </div>
                </td>
              </tr>
              <tr v-if="sortedPartitions.length === 0">
                <td colspan="6" class="text-center py-8 text-light-500 text-sm">
                  No partitions found
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </template>
    
    <!-- Error state -->
    <div v-else-if="error" class="card p-6 text-rose-600 dark:text-rose-400">
      <p class="font-semibold mb-2">Error loading queue</p>
      <p class="text-sm">{{ error }}</p>
    </div>

    <!-- Delete Queue Confirmation Modal -->
    <div 
      v-if="showDeleteModal"
      class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-dark-500/50 backdrop-blur-sm"
      @click="showDeleteModal = false"
    >
      <div 
        class="card w-full max-w-md animate-slide-up"
        @click.stop
      >
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Delete Queue</h3>
        </div>
        <div class="card-body">
          <p class="text-light-600 dark:text-light-400">
            Are you sure you want to delete <strong>{{ queueName }}</strong>? 
            This will permanently remove the queue and all its messages. This action cannot be undone.
          </p>
        </div>
        <div class="px-5 py-4 border-t border-light-200 dark:border-dark-50 flex items-center justify-end gap-3">
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
import { useRoute, useRouter } from 'vue-router'
import { analytics, queues as queuesApi } from '@/api'
import { formatNumber } from '@/composables/useApi'

const route = useRoute()
const router = useRouter()
const queueName = computed(() => route.params.queueName)

// State
const loading = ref(true)
const error = ref(null)
const queueData = ref(null)   // From /resources/queues/{name}
const statusData = ref(null)  // From /status/queues/{name}
const partitionSearch = ref('')
const partitionSortColumn = ref('name')
const partitionSortDirection = ref('asc')

const showDeleteModal = ref(false)

// Computed
const partitions = computed(() => {
  // Merge partition data from both sources
  const statusPartitions = statusData.value?.partitions || []
  const queuePartitions = queueData.value?.partitions || []
  
  // If we have status partitions (with cursor info), use those
  if (statusPartitions.length > 0) {
    return statusPartitions
  }
  
  // Otherwise fall back to queue partitions
  return queuePartitions.map(p => ({
    ...p,
    messages: p.stats || p.messages || {}
  }))
})

// Calculate totals from partitions
const totalMessages = computed(() => {
  const totals = {
    total: 0,
    pending: 0,
    processing: 0,
    completed: 0,
    failed: 0,
    deadLetter: 0
  }
  
  for (const p of partitions.value) {
    const msgs = p.messages || p.stats || {}
    totals.total += msgs.total || 0
    totals.pending += msgs.pending || 0
    totals.processing += msgs.processing || 0
    totals.completed += msgs.completed || 0
    totals.failed += msgs.failed || 0
    totals.deadLetter += msgs.deadLetter || 0
  }
  
  return totals
})

const sortedPartitions = computed(() => {
  let result = [...partitions.value]
  
  // Filter by search
  if (partitionSearch.value) {
    const search = partitionSearch.value.toLowerCase()
    result = result.filter(p => p.name.toLowerCase().includes(search))
  }
  
  // Sort
  result.sort((a, b) => {
    let aVal, bVal
    
    switch (partitionSortColumn.value) {
      case 'name':
        aVal = a.name.toLowerCase()
        bVal = b.name.toLowerCase()
        break
      case 'pending':
        aVal = a.messages?.pending || 0
        bVal = b.messages?.pending || 0
        break
      case 'processing':
        aVal = a.messages?.processing || 0
        bVal = b.messages?.processing || 0
        break
      case 'completed':
        aVal = a.messages?.completed || 0
        bVal = b.messages?.completed || 0
        break
      case 'failed':
        aVal = a.messages?.failed || 0
        bVal = b.messages?.failed || 0
        break
      default:
        return 0
    }
    
    if (typeof aVal === 'string') {
      return partitionSortDirection.value === 'asc' 
        ? aVal.localeCompare(bVal) 
        : bVal.localeCompare(aVal)
    }
    return partitionSortDirection.value === 'asc' ? aVal - bVal : bVal - aVal
  })
  
  return result
})

// Methods
const sortPartitions = (column) => {
  if (partitionSortColumn.value === column) {
    partitionSortDirection.value = partitionSortDirection.value === 'asc' ? 'desc' : 'asc'
  } else {
    partitionSortColumn.value = column
    partitionSortDirection.value = 'asc'
  }
}

const getSortClass = (column) => {
  if (partitionSortColumn.value !== column) return 'opacity-30'
  return partitionSortDirection.value === 'asc' ? '' : 'rotate-180'
}

const fetchData = async () => {
  loading.value = true
  error.value = null
  
  try {
    // Fetch queue metadata and status in parallel
    const [queueResponse, statusResponse] = await Promise.all([
      queuesApi.get(queueName.value).catch(() => null),
      analytics.getQueueDetail(queueName.value).catch(() => null)
    ])
    
    queueData.value = queueResponse?.data || null
    statusData.value = statusResponse?.data || null
    
    // If neither returned data, show error
    if (!queueData.value && !statusData.value) {
      error.value = 'Queue not found'
    }
  } catch (err) {
    error.value = err.response?.data?.error || err.message
  } finally {
    loading.value = false
  }
}

const formatDate = (dateStr) => {
  if (!dateStr) return '-'
  return new Date(dateStr).toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit'
  })
}

const formatTime = (dateStr) => {
  if (!dateStr) return '-'
  return new Date(dateStr).toLocaleTimeString('en-US', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  })
}

const formatDuration = (seconds) => {
  if (!seconds || seconds === 0) return '0s'
  if (seconds < 60) return `${seconds}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h`
  return `${Math.floor(seconds / 86400)}d`
}

const deleteQueue = async () => {
  try {
    await queuesApi.delete(queueName.value)
    showDeleteModal.value = false
    router.push('/queues')
  } catch (err) {
    console.error('Failed to delete queue:', err)
  }
}

onMounted(fetchData)
</script>
