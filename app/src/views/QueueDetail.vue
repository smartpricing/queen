<template>
  <div class="view-container">

    <!-- Loading state -->
    <div v-if="loading" style="display:flex; flex-direction:column; gap:20px;">
      <div class="card" style="padding:20px;">
        <div class="skeleton" style="height:32px; width:192px; margin-bottom:16px;" />
        <div class="skeleton" style="height:16px; width:256px;" />
      </div>
      <div class="grid-4">
        <div v-for="i in 4" :key="i" class="stat">
          <div class="skeleton" style="height:16px; width:80px; margin-bottom:8px;" />
          <div class="skeleton" style="height:32px; width:64px;" />
        </div>
      </div>
    </div>

    <template v-else-if="queueData || statusData">
      <!-- Compact header: back · queue name · meta · actions -->
      <div class="detail-bar">
        <button @click="$router.push('/queues')" class="detail-back" title="Back to queues">
          <svg style="width:14px; height:14px;" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.8">
            <path stroke-linecap="round" stroke-linejoin="round" d="M10 19l-7-7m0 0l7-7m-7 7h18" />
          </svg>
        </button>
        <span class="detail-name font-mono">{{ queueData?.name || queueName }}</span>
        <span class="detail-meta font-mono">
          <span v-if="queueData?.namespace">{{ queueData.namespace }}</span>
          <span v-if="queueData?.task">· {{ queueData.task }}</span>
          <span>· {{ partitions.length }} partitions</span>
          <span v-if="queueData?.createdAt">· {{ formatDate(queueData.createdAt) }}</span>
        </span>
        <div class="detail-actions">
          <button @click="showDeleteModal = true" class="btn btn-danger">
            <svg style="width:14px; height:14px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
            </svg>
            Delete queue
          </button>
        </div>
      </div>

      <!-- Metrics Grid -->
      <div style="display:grid; grid-template-columns:repeat(5,1fr); gap:16px; margin-bottom:20px;">
        <div class="stat">
          <div class="stat-label">Total</div>
          <div class="stat-value font-mono">{{ formatNumber(totalMessages.total) }}</div>
        </div>
        <div class="stat">
          <div class="stat-label">Pending</div>
          <div class="stat-value font-mono num" :class="{ warn: Math.max(0, totalMessages.pending) > 1000 && Math.max(0, totalMessages.pending) < 10000, bad: Math.max(0, totalMessages.pending) >= 10000 }">{{ formatNumber(Math.max(0, totalMessages.pending)) }}</div>
        </div>
        <div class="stat">
          <div class="stat-label">Processing</div>
          <div class="stat-value font-mono">{{ formatNumber(totalMessages.processing) }}</div>
        </div>
        <div class="stat">
          <div class="stat-label">Completed</div>
          <div class="stat-value font-mono">{{ formatNumber(totalMessages.completed) }}</div>
        </div>
        <div class="stat">
          <div class="stat-label">Failed / DLQ</div>
          <div class="stat-value font-mono num" :class="{ bad: (totalMessages.failed + totalMessages.deadLetter) > 0 }">{{ formatNumber(totalMessages.failed + totalMessages.deadLetter) }}</div>
        </div>
      </div>

      <!-- Queue Configuration -->
      <div v-if="queueData?.config" class="card" style="padding:20px; margin-bottom:20px;">
        <h3 style="font-size:13px; font-weight:600; color:var(--text-hi); margin-bottom:16px;">Queue Configuration</h3>
        <div class="grid-3" style="gap:12px 24px;">
          <div>
            <span class="label-xs">Lease Time</span>
            <span style="font-weight:600; display:block; font-family:'JetBrains Mono',monospace;">{{ queueData.config.leaseTime || 0 }}s</span>
          </div>
          <div>
            <span class="label-xs">TTL</span>
            <span style="font-weight:600; display:block; font-family:'JetBrains Mono',monospace;">{{ formatDuration(queueData.config.ttl || 0) }}</span>
          </div>
          <div>
            <span class="label-xs">Max Queue Size</span>
            <span style="font-weight:600; display:block; font-family:'JetBrains Mono',monospace;">{{ formatNumber(queueData.config.maxQueueSize || 0) }}</span>
          </div>
          <div>
            <span class="label-xs">Retry Limit</span>
            <span style="font-weight:600; display:block; font-family:'JetBrains Mono',monospace;">{{ queueData.config.retryLimit || 0 }}</span>
          </div>
          <div>
            <span class="label-xs">Retry Delay</span>
            <span style="font-weight:600; display:block; font-family:'JetBrains Mono',monospace;">{{ queueData.config.retryDelay || 0 }}ms</span>
          </div>
          <div>
            <span class="label-xs">DLQ Enabled</span>
            <span style="font-weight:600; display:block;">{{ queueData.config.deadLetterQueue ? 'Yes' : 'No' }}</span>
          </div>
        </div>
      </div>

      <!-- Partitions Table -->
      <div class="card">
        <div class="card-header" style="justify-content:space-between;">
          <div style="display:flex; align-items:center; gap:10px;">
            <h3>Partitions</h3>
            <span class="chip chip-ice">{{ partitions.length }} partitions</span>
          </div>
          <div style="width:192px;">
            <input
              v-model="partitionSearch"
              type="text"
              placeholder="Search partitions..."
              class="input"
              style="font-size:12px;"
            />
          </div>
        </div>

        <div style="overflow-x:auto;">
          <table class="t" style="width:100%;">
            <thead>
              <tr>
                <th @click="sortPartitions('name')" style="cursor:pointer;">
                  <div style="display:flex; align-items:center; gap:4px;">
                    Partition
                    <svg style="width:12px; height:12px; transition:transform .2s;" :class="getSortClass('name')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('pending')" style="text-align:right; cursor:pointer;">
                  <div style="display:flex; align-items:center; justify-content:flex-end; gap:4px;">
                    Pending
                    <svg style="width:12px; height:12px; transition:transform .2s;" :class="getSortClass('pending')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('processing')" style="text-align:right; cursor:pointer;">
                  <div style="display:flex; align-items:center; justify-content:flex-end; gap:4px;">
                    Processing
                    <svg style="width:12px; height:12px; transition:transform .2s;" :class="getSortClass('processing')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('completed')" style="text-align:right; cursor:pointer;">
                  <div style="display:flex; align-items:center; justify-content:flex-end; gap:4px;">
                    Completed
                    <svg style="width:12px; height:12px; transition:transform .2s;" :class="getSortClass('completed')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th @click="sortPartitions('failed')" style="text-align:right; cursor:pointer;">
                  <div style="display:flex; align-items:center; justify-content:flex-end; gap:4px;">
                    Failed
                    <svg style="width:12px; height:12px; transition:transform .2s;" :class="getSortClass('failed')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                    </svg>
                  </div>
                </th>
                <th style="text-align:right;">Consumed</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="partition in sortedPartitions" :key="partition.id">
                <td>
                  <div style="font-weight:500; color:var(--text-hi);">{{ partition.name }}</div>
                  <div v-if="partition.lastActivity" style="font-size:11px; color:var(--text-low); margin-top:2px;">
                    Last: {{ formatTime(partition.lastActivity) }}
                  </div>
                </td>
                <td style="text-align:right;" class="font-mono tabular-nums">{{ formatNumber(Math.max(0, partition.messages?.pending || 0)) }}</td>
                <td style="text-align:right;" class="font-mono tabular-nums">{{ formatNumber(partition.messages?.processing || 0) }}</td>
                <td style="text-align:right;" class="font-mono tabular-nums">
                  {{ formatNumber(partition.messages?.completed || 0) }}
                </td>
                <td style="text-align:right;" class="font-mono tabular-nums num" :class="{ bad: (partition.messages?.failed || 0) > 0 }">
                  {{ formatNumber(partition.messages?.failed || 0) }}
                </td>
                <td style="text-align:right;">
                  <div class="font-mono tabular-nums" style="font-size:13px;">{{ formatNumber(partition.cursor?.totalConsumed || 0) }}</div>
                  <div style="font-size:11px; color:var(--text-low);">
                    {{ partition.cursor?.batchesConsumed || 0 }} batches
                  </div>
                </td>
              </tr>
              <tr v-if="sortedPartitions.length === 0">
                <td colspan="6" style="text-align:center; padding:32px; color:var(--text-low); font-size:13px;">
                  No partitions found
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </template>

    <!-- Error state -->
    <div v-else-if="error" class="card" style="padding:24px; color:#f43f5e;">
      <p style="font-weight:600; margin-bottom:8px;">Error loading queue</p>
      <p style="font-size:13px;">{{ error }}</p>
    </div>

    <!-- Delete Queue Confirmation Modal -->
    <div
      v-if="showDeleteModal"
      style="position:fixed; inset:0; z-index:50; display:flex; align-items:center; justify-content:center; padding:16px; background:rgba(0,0,0,.5); backdrop-filter:blur(4px);"
      @click="showDeleteModal = false"
    >
      <div
        class="card animate-slide-up"
        style="width:100%; max-width:420px;"
        @click.stop
      >
        <div class="card-header">
          <h3>Delete Queue</h3>
        </div>
        <div class="card-body">
          <p style="color:var(--text-mid);">
            Are you sure you want to delete <strong>{{ queueName }}</strong>?
            This will permanently remove the queue and all its messages. This action cannot be undone.
          </p>
        </div>
        <div style="padding:14px 16px; border-top:1px solid var(--bd); display:flex; align-items:center; justify-content:flex-end; gap:12px;">
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

// Calculate totals - prefer pre-computed totals from API, fallback to summing partitions
const totalMessages = computed(() => {
  // First, try to use pre-computed totals from statusData (get_queue_detail_v2)
  const apiTotals = statusData.value?.totals
  if (apiTotals) {
    // get_queue_detail_v2 returns totals.messages.* and also totals.* (flat)
    const msgs = apiTotals.messages || apiTotals
    return {
      total: msgs.total || 0,
      pending: msgs.pending || 0,
      processing: msgs.processing || 0,
      completed: msgs.completed || 0,
      failed: msgs.failed || 0,
      deadLetter: msgs.deadLetter || 0
    }
  }
  
  // Fallback: sum from partitions
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
  // Only show loading on initial load
  if (!queueData.value && !statusData.value) {
    loading.value = true
  }
  error.value = null
  
  try {
    // Fetch queue metadata and status in parallel
    const [queueResponse, statusResponse] = await Promise.all([
      queuesApi.get(queueName.value).catch(() => null),
      analytics.getQueueDetail(queueName.value).catch(() => null)
    ])
    
    // get_queue_v2 returns flat structure: { id, name, namespace, task, partitions, totals }
    // get_queue_detail_v2 returns: { queue: { id, name, config, ... }, partitions, totals }
    
    // Merge the data - prefer statusResponse for partitions (has cursor info)
    // but extract queue info and config properly
    const rawQueue = queueResponse?.data || null
    const rawStatus = statusResponse?.data || null
    
    // Build queueData by merging both sources
    if (rawStatus?.queue) {
      // get_queue_detail_v2 wraps queue info under 'queue' key
      queueData.value = {
        ...rawStatus.queue,
        // Add config from the nested queue object
        config: rawStatus.queue.config
      }
    } else if (rawQueue) {
      // Fallback to get_queue_v2 (no config available)
      queueData.value = rawQueue
    } else {
      queueData.value = null
    }
    
    // statusData holds partitions with cursor info
    statusData.value = rawStatus
    
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
