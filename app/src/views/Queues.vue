<template>
  <div class="view-container">

    <!-- Page head -->
    <!-- Stat tiles -->
    <div style="display:grid; grid-template-columns:repeat(5,1fr); gap:16px; margin-bottom:20px;">
      <div class="stat">
        <div class="stat-label">Total Queues</div>
        <div class="stat-value font-mono">{{ formatNumber(filteredQueues.length) }}</div>
        <div class="stat-foot">{{ formatNumber(totalPartitions) }} partitions</div>
      </div>
      <div class="stat">
        <div class="stat-label">Total Messages</div>
        <div class="stat-value font-mono">{{ formatNumber(totalMessages) }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Pending</div>
        <div class="stat-value font-mono" :class="{ 'num': true, 'warn': totalPending >= 1000 && totalPending < 10000, 'bad': totalPending >= 10000 }">{{ formatNumber(totalPending) }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Processing</div>
        <div class="stat-value font-mono">{{ formatNumber(totalProcessing) }}</div>
      </div>
      <div class="stat">
        <div class="stat-label">Active Queues</div>
        <div class="stat-value font-mono">{{ formatNumber(activeQueues) }}</div>
      </div>
    </div>

    <!-- Filters -->
    <div class="card" style="padding:14px 16px; margin-bottom:20px;">
      <div style="display:flex; flex-wrap:wrap; align-items:center; gap:12px;">
        <div style="flex:1; min-width:200px; max-width:360px; position:relative;">
          <input
            v-model="searchQuery"
            type="text"
            placeholder="Search queues..."
            class="input"
            style="padding-left:36px;"
          />
          <svg
            style="position:absolute; left:10px; top:50%; transform:translateY(-50%); width:16px; height:16px; color:var(--text-low);"
            fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
          >
            <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
          </svg>
        </div>

        <select v-model="filterNamespace" class="input" style="width:170px;">
          <option value="">All Namespaces</option>
          <option v-for="ns in namespaces" :key="ns" :value="ns">
            {{ ns || '(empty)' }}
          </option>
        </select>

        <select v-model="filterTask" class="input" style="width:170px;">
          <option value="">All Tasks</option>
          <option v-for="task in tasks" :key="task" :value="task">
            {{ task || '(empty)' }}
          </option>
        </select>

        <select v-model="sortBy" class="input" style="width:170px;">
          <option value="name">Sort by Name</option>
          <option value="pending">Sort by Pending</option>
          <option value="total">Sort by Total</option>
        </select>
      </div>
    </div>

    <!-- Queues Table -->
    <div class="card" style="overflow:hidden;">
      <table class="t" style="width:100%;">
        <thead>
          <tr>
            <th style="text-align:left;">Queue Name</th>
            <th style="text-align:right;">Partitions</th>
            <th style="text-align:right;">Pending</th>
            <th style="text-align:right;">Total</th>
            <th style="text-align:center;">Actions</th>
          </tr>
        </thead>
        <tbody v-if="loading">
          <tr v-for="i in 5" :key="i">
            <td><div class="skeleton" style="height:16px; width:128px;" /></td>
            <td><div class="skeleton" style="height:16px; width:32px; margin-left:auto;" /></td>
            <td><div class="skeleton" style="height:16px; width:64px; margin-left:auto;" /></td>
            <td><div class="skeleton" style="height:16px; width:64px; margin-left:auto;" /></td>
            <td><div class="skeleton" style="height:16px; width:64px; margin:0 auto;" /></td>
          </tr>
        </tbody>
        <tbody v-else-if="filteredQueues.length > 0">
          <tr
            v-for="queue in filteredQueues"
            :key="queue.name"
            style="cursor:pointer;"
            @click="$router.push(`/queues/${queue.name}`)"
          >
            <td>
              <span style="font-weight:500; color:var(--text-hi);">{{ queue.name }}</span>
            </td>
            <td style="text-align:right;" class="font-mono tabular-nums">
              {{ queue.partitions || 1 }}
            </td>
            <td style="text-align:right;" class="font-mono tabular-nums">
              <span class="num" :class="{ warn: Math.max(0, queue.messages?.pending || 0) >= 1000 && Math.max(0, queue.messages?.pending || 0) < 10000, bad: Math.max(0, queue.messages?.pending || 0) >= 10000 }">{{ formatNumber(Math.max(0, queue.messages?.pending || 0)) }}</span>
            </td>
            <td style="text-align:right; font-weight:600; color:var(--text-hi);" class="font-mono tabular-nums">
              {{ formatNumber(queue.messages?.total || 0) }}
            </td>
            <td style="text-align:center;">
              <div style="display:flex; align-items:center; justify-content:center; gap:4px;">
                <button
                  @click.stop="viewQueue(queue)"
                  class="btn-ghost"
                  style="padding:6px; border-radius:6px;"
                  title="View details"
                >
                  <svg style="width:16px; height:16px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M2.036 12.322a1.012 1.012 0 010-.639C3.423 7.51 7.36 4.5 12 4.5c4.638 0 8.573 3.007 9.963 7.178.07.207.07.431 0 .639C20.577 16.49 16.64 19.5 12 19.5c-4.638 0-8.573-3.007-9.963-7.178z" />
                    <path stroke-linecap="round" stroke-linejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
                  </svg>
                </button>
                <button
                  @click.stop="confirmDelete(queue)"
                  class="row-action-danger"
                  title="Delete queue"
                >
                  <svg style="width:16px; height:16px;" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M14.74 9l-.346 9m-4.788 0L9.26 9m9.968-3.21c.342.052.682.107 1.022.166m-1.022-.165L18.16 19.673a2.25 2.25 0 01-2.244 2.077H8.084a2.25 2.25 0 01-2.244-2.077L4.772 5.79m14.456 0a48.108 48.108 0 00-3.478-.397m-12 .562c.34-.059.68-.114 1.022-.165m0 0a48.11 48.11 0 013.478-.397m7.5 0v-.916c0-1.18-.91-2.164-2.09-2.201a51.964 51.964 0 00-3.32 0c-1.18.037-2.09 1.022-2.09 2.201v.916m7.5 0a48.667 48.667 0 00-7.5 0" />
                  </svg>
                </button>
              </div>
            </td>
          </tr>
        </tbody>
        <tbody v-else>
          <tr>
            <td colspan="5" style="text-align:center; padding:48px 16px;">
              <svg style="width:48px; height:48px; margin:0 auto 12px; color:var(--text-low);" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1">
                <path stroke-linecap="round" stroke-linejoin="round" d="M3.75 12h16.5m-16.5 3.75h16.5M3.75 19.5h16.5M5.625 4.5h12.75a1.875 1.875 0 010 3.75H5.625a1.875 1.875 0 010-3.75z" />
              </svg>
              <h3 style="font-size:13px; font-weight:600; color:var(--text-hi); margin-bottom:4px;">No queues found</h3>
              <p style="font-size:13px; color:var(--text-mid);">
                {{ searchQuery || filterNamespace || filterTask ? 'Try adjusting your filters' : 'Create a queue to get started' }}
              </p>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Delete confirmation modal -->
    <div
      v-if="showDeleteModal"
      style="position:fixed; inset:0; z-index:50; display:flex; align-items:center; justify-content:center; padding:16px; background:rgba(0,0,0,.5); backdrop-filter:blur(4px);"
      @click="showDeleteModal = false"
    >
      <div
        class="card animate-slide-up"
        style="padding:24px; width:100%; max-width:420px;"
        @click.stop
      >
        <h3 style="font-size:16px; font-weight:600; color:var(--text-hi); margin-bottom:8px;">
          Delete Queue
        </h3>
        <p style="color:var(--text-mid); margin-bottom:24px;">
          Are you sure you want to delete <strong>{{ queueToDelete?.name }}</strong>? This will permanently remove the queue and all its messages. This action cannot be undone.
        </p>
        <div style="display:flex; align-items:center; justify-content:flex-end; gap:12px;">
          <button @click="showDeleteModal = false" class="btn btn-ghost">
            Cancel
          </button>
          <button @click="deleteQueue" class="btn btn-danger">
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
  filteredQueues.value.reduce((sum, q) => sum + Math.max(0, q.messages?.pending || 0), 0)
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
  // Only show loading skeleton if we don't have data yet (smooth background refresh)
  if (!queues.value.length) loading.value = true
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
