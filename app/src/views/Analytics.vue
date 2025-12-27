<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Filters -->
    <div class="card p-4">
      <div class="flex flex-col gap-4">
        <!-- Time Range Row -->
        <div class="flex flex-wrap items-center gap-3">
          <label class="text-xs font-medium text-light-600 dark:text-light-400">Time Range:</label>
          <div class="flex items-center gap-1 flex-wrap">
            <button 
              v-for="range in timeRanges" 
              :key="range.value"
              @click="selectQuickRange(range.value)"
              class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
              :class="timeRange === range.value && !customMode
                ? 'bg-queen-500 text-white' 
                : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
            >
              {{ range.label }}
            </button>
            <button 
              @click="toggleCustomMode"
              class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
              :class="customMode
                ? 'bg-queen-500 text-white' 
                : 'bg-light-100 dark:bg-dark-300 text-light-700 dark:text-light-300 hover:bg-light-200 dark:hover:bg-dark-200'"
            >
              Custom
            </button>
          </div>
        </div>
        
        <!-- Custom Date/Time Range -->
        <div v-if="customMode" class="flex flex-wrap items-center gap-3 pt-3 border-t border-light-200 dark:border-dark-50">
          <div class="flex items-center gap-2">
            <label class="text-xs font-medium text-light-600 dark:text-light-400 whitespace-nowrap">From:</label>
            <input 
              type="datetime-local" 
              v-model="customFrom"
              class="input text-sm py-1.5 px-2 font-mono"
            />
          </div>
          <div class="flex items-center gap-2">
            <label class="text-xs font-medium text-light-600 dark:text-light-400 whitespace-nowrap">To:</label>
            <input 
              type="datetime-local" 
              v-model="customTo"
              class="input text-sm py-1.5 px-2 font-mono"
            />
          </div>
          <button 
            @click="applyCustomRange"
            class="btn btn-primary text-xs"
          >
            Apply
          </button>
        </div>
        
        <!-- Filters Row -->
        <div class="flex flex-wrap items-end gap-3">
          <!-- Queue Filter -->
          <div class="w-48">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">Queue</label>
            <select v-model="queueFilter" class="select">
              <option value="">All Queues</option>
              <option v-for="q in allQueues" :key="q.name" :value="q.name">
                {{ q.name }}
              </option>
            </select>
          </div>
          
          <!-- Namespace Filter -->
          <div class="w-40">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">Namespace</label>
            <select v-model="namespaceFilter" class="select">
              <option value="">All Namespaces</option>
              <option v-for="ns in namespaces" :key="ns.namespace" :value="ns.namespace">
                {{ ns.namespace || 'Default' }}
              </option>
            </select>
          </div>
          
          <!-- Task Filter -->
          <div class="w-40">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">Task</label>
            <select v-model="taskFilter" class="select">
              <option value="">All Tasks</option>
              <option v-for="task in tasks" :key="task.task" :value="task.task">
                {{ task.task || 'Default' }}
              </option>
            </select>
          </div>
          
          <button 
            v-if="queueFilter || namespaceFilter || taskFilter"
            @click="clearFilters"
            class="btn btn-ghost text-xs"
          >
            Clear Filters
          </button>
        </div>
      </div>
      
      <!-- Active Filters Display -->
      <div v-if="queueFilter || namespaceFilter || taskFilter" class="flex flex-wrap gap-2 mt-3 pt-3 border-t border-light-200 dark:border-dark-50">
        <span v-if="queueFilter" class="badge badge-queen">
          Queue: {{ queueFilter }}
        </span>
        <span v-if="namespaceFilter" class="badge badge-cyber">
          Namespace: {{ namespaceFilter }}
        </span>
        <span v-if="taskFilter" class="badge badge-crown">
          Task: {{ taskFilter }}
        </span>
      </div>
    </div>

    <div v-if="loading" class="space-y-4 sm:space-y-6">
      <div class="card p-6">
        <div class="skeleton h-64 w-full rounded-lg" />
      </div>
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <div class="card p-6">
          <div class="skeleton h-48 w-full rounded-lg" />
        </div>
        <div class="card p-6">
          <div class="skeleton h-48 w-full rounded-lg" />
        </div>
      </div>
    </div>

    <template v-else-if="statusData">
      <!-- Message Flow Chart -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Message Flow Over Time</h3>
        </div>
        <div class="card-body">
          <BaseChart 
            v-if="throughputData.labels.length > 0"
            type="line" 
            :data="throughputData" 
            :options="chartOptions" 
            height="280px"
          />
          <div v-else class="text-center py-12 text-light-500">
            No throughput data available
          </div>
        </div>
      </div>

      <!-- Charts Row -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <!-- Top Queues Chart -->
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Top Queues by Volume</h3>
          </div>
          <div class="card-body">
            <BaseChart 
              v-if="queueActivityData.labels.length > 0"
              type="bar" 
              :data="queueActivityData" 
              :options="barChartOptions" 
              height="240px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No queue data available
            </div>
          </div>
        </div>
        
        <!-- Message Distribution Chart -->
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Message Status Distribution</h3>
          </div>
          <div class="card-body flex items-center justify-center">
            <BaseChart 
              v-if="messageDistributionData.labels.length > 0"
              type="doughnut" 
              :data="messageDistributionData" 
              :options="doughnutOptions" 
              height="240px"
            />
            <div v-else class="text-center py-12 text-light-500">
              No message data available
            </div>
          </div>
        </div>
      </div>

      <!-- Performance Metrics -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Message Counts</h3>
        </div>
        <div class="card-body">
          <div class="grid grid-cols-2 md:grid-cols-5 gap-2 sm:gap-4">
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Pending</p>
              <p class="text-2xl font-bold text-cyber-600 dark:text-cyber-400 mt-1">
                {{ formatNumber(statusData?.messages?.pending || 0) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Processing</p>
              <p class="text-2xl font-bold text-crown-600 dark:text-crown-400 mt-1">
                {{ formatNumber(statusData?.messages?.processing || 0) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Completed</p>
              <p class="text-2xl font-bold text-emerald-600 dark:text-emerald-400 mt-1">
                {{ formatNumber(statusData?.messages?.completed || 0) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Failed</p>
              <p class="text-2xl font-bold text-orange-600 dark:text-orange-400 mt-1">
                {{ formatNumber(statusData?.messages?.failed || 0) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg">
              <p class="text-xs text-light-500 uppercase tracking-wide">Dead Letter</p>
              <p class="text-2xl font-bold text-rose-600 dark:text-rose-400 mt-1">
                {{ formatNumber(statusData?.messages?.deadLetter || 0) }}
              </p>
            </div>
          </div>
        </div>
      </div>

      <!-- Detailed Stats -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
        <!-- Leases Info -->
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Active Leases</h3>
          </div>
          <div class="card-body space-y-3">
            <div class="flex items-center justify-between">
              <span class="text-light-600 dark:text-light-400">Active Leases</span>
              <span class="font-semibold">{{ statusData?.leases?.active || 0 }}</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-light-600 dark:text-light-400">Partitions with Leases</span>
              <span class="font-semibold">{{ statusData?.leases?.partitionsWithLeases || 0 }}</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-light-600 dark:text-light-400">Total Batch Size</span>
              <span class="font-semibold">{{ statusData?.leases?.totalBatchSize || 0 }}</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-light-600 dark:text-light-400">Total Acked</span>
              <span class="font-semibold">{{ statusData?.leases?.totalAcked || 0 }}</span>
            </div>
          </div>
        </div>

        <!-- Dead Letter Queue -->
        <div class="card">
          <div class="card-header">
            <h3 class="font-semibold text-light-900 dark:text-white">Dead Letter Queue</h3>
          </div>
          <div class="card-body space-y-3">
            <div class="flex items-center justify-between">
              <span class="text-light-600 dark:text-light-400">Total Messages</span>
              <span class="font-semibold">{{ statusData?.deadLetterQueue?.totalMessages || 0 }}</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-light-600 dark:text-light-400">Affected Partitions</span>
              <span class="font-semibold">{{ statusData?.deadLetterQueue?.affectedPartitions || 0 }}</span>
            </div>
            
            <div v-if="statusData?.deadLetterQueue?.topErrors?.length" class="pt-3 border-t border-light-200 dark:border-dark-50">
              <p class="text-xs font-semibold text-light-600 dark:text-light-400 mb-2">Top Errors:</p>
              <div class="space-y-2">
                <div
                  v-for="(errorItem, idx) in statusData.deadLetterQueue.topErrors.slice(0, 3)"
                  :key="idx"
                  class="flex items-center justify-between text-sm"
                >
                  <span class="text-light-600 dark:text-light-400 truncate flex-1">{{ errorItem.error }}</span>
                  <span class="font-semibold ml-2">{{ errorItem.count }}</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Workers Info -->
      <div v-if="statusData?.workers?.length" class="card">
        <div class="card-header flex items-center justify-between">
          <h3 class="font-semibold text-light-900 dark:text-white">{{ statusData.workers.length }} Workers</h3>
          <span class="badge badge-success">Healthy</span>
        </div>
        <div class="card-body">
          <div class="grid grid-cols-3 gap-2 sm:gap-4">
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg text-center">
              <p class="text-xs text-light-500 uppercase tracking-wide">Avg Event Loop</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ Math.round(statusData.workers.reduce((sum, w) => sum + (w.avgEventLoopLagMs || 0), 0) / statusData.workers.length) }}ms
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg text-center">
              <p class="text-xs text-light-500 uppercase tracking-wide">Connection Pool</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ statusData.workers.reduce((sum, w) => sum + (w.freeSlots || 0), 0) }}/{{ statusData.workers.reduce((sum, w) => sum + (w.dbConnections || 0), 0) }}
              </p>
            </div>
            <div class="p-4 bg-light-100 dark:bg-dark-300 rounded-lg text-center">
              <p class="text-xs text-light-500 uppercase tracking-wide">Max Job Queue</p>
              <p class="text-2xl font-bold text-light-900 dark:text-light-100 mt-1">
                {{ Math.max(...statusData.workers.map(w => w.jobQueueSize || 0)) }}
              </p>
            </div>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted } from 'vue'
import { analytics, queues as queuesApi, resources } from '@/api'
import { formatNumber } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'
import BaseChart from '@/components/BaseChart.vue'

// State
const loading = ref(true)
const statusData = ref(null)
const allQueues = ref([])
const namespaces = ref([])
const tasks = ref([])

const timeRange = ref('1h')
const customMode = ref(false)
const customFrom = ref('')
const customTo = ref('')
const queueFilter = ref('')
const namespaceFilter = ref('')
const taskFilter = ref('')

const timeRanges = [
  { label: '1h', value: '1h' },
  { label: '6h', value: '6h' },
  { label: '24h', value: '24h' },
  { label: '7d', value: '7d' }
]

// Format date to datetime-local input format
const formatDateTimeLocal = (date) => {
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hours = String(date.getHours()).padStart(2, '0')
  const minutes = String(date.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day}T${hours}:${minutes}`
}

// Chart Options
const chartOptions = {
  plugins: {
    legend: {
      display: true,
      position: 'top',
      labels: {
        usePointStyle: true,
        padding: 20
      }
    }
  },
  scales: {
    y: {
      title: { display: true, text: 'Messages', font: { size: 11 } }
    }
  }
}

const barChartOptions = {
  plugins: {
    legend: { display: false }
  },
  scales: {
    x: {
      stacked: true
    },
    y: {
      stacked: true,
      beginAtZero: true,
      title: { display: true, text: 'Messages', font: { size: 11 } }
    }
  }
}

const doughnutOptions = {
  plugins: {
    legend: {
      display: true,
      position: 'bottom',
      labels: {
        usePointStyle: true,
        padding: 16
      }
    }
  },
  cutout: '60%'
}

// Format chart label based on time span
const formatChartLabel = (date, isMultiDay) => {
  if (isMultiDay) {
    return date.toLocaleString('en-US', { 
      month: 'short', 
      day: 'numeric', 
      hour: '2-digit', 
      minute: '2-digit' 
    })
  }
  return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
}

// Computed - Chart data
const throughputData = computed(() => {
  if (!statusData.value?.throughput || !Array.isArray(statusData.value.throughput)) {
    return { labels: [], datasets: [] }
  }
  
  // Reverse to get chronological order
  const history = [...statusData.value.throughput].reverse()
  
  // Check if data spans multiple days
  let isMultiDay = false
  if (history.length > 1) {
    const firstDate = new Date(history[0].timestamp)
    const lastDate = new Date(history[history.length - 1].timestamp)
    isMultiDay = firstDate.toDateString() !== lastDate.toDateString()
  }
  
  const labels = history.map(h => {
    const date = new Date(h.timestamp)
    return formatChartLabel(date, isMultiDay)
  })
  
  return {
    labels,
    datasets: [
      {
        label: 'Ingested',
        data: history.map(h => h.ingested || 0),
        fill: true,
      },
      {
        label: 'Processed',
        data: history.map(h => h.processed || 0),
        fill: true,
      }
    ]
  }
})

const queueActivityData = computed(() => {
  if (!allQueues.value.length) {
    return { labels: [], datasets: [] }
  }
  
  // Apply filters locally as well (in case API doesn't support all filters)
  let filtered = [...allQueues.value]
  if (queueFilter.value) {
    filtered = filtered.filter(q => q.name === queueFilter.value)
  }
  if (namespaceFilter.value) {
    filtered = filtered.filter(q => q.namespace === namespaceFilter.value)
  }
  if (taskFilter.value) {
    filtered = filtered.filter(q => q.task === taskFilter.value)
  }
  
  const sorted = filtered
    .sort((a, b) => (b.messages?.total || 0) - (a.messages?.total || 0))
    .slice(0, 8)
  
  return {
    labels: sorted.map(q => q.name.length > 25 ? q.name.substring(0, 25) + '...' : q.name),
    datasets: [
      {
        label: 'Pending',
        data: sorted.map(q => q.messages?.pending || 0),
        backgroundColor: 'rgba(6, 182, 212, 0.7)',
      },
      {
        label: 'Processing',
        data: sorted.map(q => q.messages?.processing || 0),
        backgroundColor: 'rgba(245, 158, 11, 0.7)',
      },
      {
        label: 'Completed',
        data: sorted.map(q => q.messages?.completed || 0),
        backgroundColor: 'rgba(16, 185, 129, 0.7)',
      }
    ]
  }
})

const messageDistributionData = computed(() => {
  if (!statusData.value?.messages) {
    return { labels: [], datasets: [] }
  }
  
  const data = statusData.value.messages
  
  // Build data array - only include non-zero values to avoid chart clutter
  const entries = [
    { label: 'Pending', value: data.pending || 0, color: 'rgba(6, 182, 212, 0.8)' },
    { label: 'Processing', value: data.processing || 0, color: 'rgba(245, 158, 11, 0.8)' },
    { label: 'Completed', value: data.completed || 0, color: 'rgba(16, 185, 129, 0.8)' },
    { label: 'Failed', value: data.failed || 0, color: 'rgba(249, 115, 22, 0.8)' },
    { label: 'Dead Letter', value: data.deadLetter || 0, color: 'rgba(244, 63, 94, 0.8)' }
  ].filter(e => e.value > 0)
  
  // If all zeros, return empty to show "no data" message
  if (entries.length === 0) {
    return { labels: [], datasets: [] }
  }
  
  return {
    labels: entries.map(e => e.label),
    datasets: [{
      data: entries.map(e => e.value),
      backgroundColor: entries.map(e => e.color),
      borderWidth: 0
    }]
  }
})

// Methods
const selectQuickRange = (value) => {
  customMode.value = false
  timeRange.value = value
  fetchAnalytics()
}

const toggleCustomMode = () => {
  customMode.value = !customMode.value
  if (customMode.value) {
    // Initialize with current range when entering custom mode
    const now = new Date()
    const from = new Date(now)
    
    switch (timeRange.value) {
      case '1h':
        from.setHours(from.getHours() - 1)
        break
      case '6h':
        from.setHours(from.getHours() - 6)
        break
      case '24h':
        from.setHours(from.getHours() - 24)
        break
      case '7d':
        from.setDate(from.getDate() - 7)
        break
    }
    
    customTo.value = formatDateTimeLocal(now)
    customFrom.value = formatDateTimeLocal(from)
  }
}

const applyCustomRange = () => {
  if (!customFrom.value || !customTo.value) {
    return
  }
  
  const fromDate = new Date(customFrom.value)
  const toDate = new Date(customTo.value)
  
  if (fromDate >= toDate) {
    return
  }
  
  fetchAnalytics()
}

const fetchAnalytics = async () => {
  // Only show loading skeleton if we don't have data yet (smooth background refresh)
  if (!statusData.value) loading.value = true
  try {
    let from, to
    
    if (customMode.value && customFrom.value && customTo.value) {
      // Use custom range
      from = new Date(customFrom.value)
      to = new Date(customTo.value)
    } else {
      // Use quick range
      const now = new Date()
      from = new Date(now)
      
      switch (timeRange.value) {
        case '1h':
          from.setHours(from.getHours() - 1)
          break
        case '6h':
          from.setHours(from.getHours() - 6)
          break
        case '24h':
          from.setHours(from.getHours() - 24)
          break
        case '7d':
          from.setDate(from.getDate() - 7)
          break
      }
      
      to = now
    }
    
    const params = {
      from: from.toISOString(),
      to: to.toISOString()
    }
    
    if (queueFilter.value) params.queue = queueFilter.value
    if (namespaceFilter.value) params.namespace = namespaceFilter.value
    if (taskFilter.value) params.task = taskFilter.value
    
    const [statusRes, queuesRes, namespacesRes, tasksRes] = await Promise.all([
      analytics.getStatus(params),
      queuesApi.list({ namespace: namespaceFilter.value, task: taskFilter.value }),
      resources.getNamespaces(),
      resources.getTasks()
    ])
    
    statusData.value = statusRes.data
    allQueues.value = queuesRes.data?.queues || []
    namespaces.value = namespacesRes.data?.namespaces || []
    tasks.value = tasksRes.data?.tasks || []
  } catch (err) {
    console.error('Failed to fetch analytics:', err)
  } finally {
    loading.value = false
  }
}

const clearFilters = () => {
  queueFilter.value = ''
  namespaceFilter.value = ''
  taskFilter.value = ''
  fetchAnalytics()
}

// Watch for filter changes
watch([queueFilter, namespaceFilter, taskFilter], () => {
  fetchAnalytics()
})

// Register for global refresh
useRefresh(fetchAnalytics)

onMounted(fetchAnalytics)
</script>
