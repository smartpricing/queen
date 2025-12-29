<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Metrics grid -->
    <div class="grid grid-cols-2 sm:grid-cols-2 lg:grid-cols-4 gap-3 sm:gap-5">
      <MetricCard
        label="Total Queues"
        :value="overview?.queues || 0"
        :subtext="`${formatNumber(totalPartitions)} partitions`"
        :icon="QueuesIcon"
        icon-color="queen"
        :loading="loadingOverview"
        clickable
        @click="$router.push('/queues')"
      />
      <MetricCard
        label="Total Messages"
        :value="overview?.messages?.total || 0"
        :subtext="`${formatNumber(overview?.messages?.completed || 0)} completed`"
        :icon="MessagesIcon"
        icon-color="cyber"
        :loading="loadingOverview"
        clickable
        @click="$router.push('/messages')"
      />
      <MetricCard
        label="Consumer Groups"
        :value="consumers?.length || 0"
        :icon="ConsumersIcon"
        icon-color="crown"
        :loading="loadingConsumers"
        clickable
        @click="$router.push('/consumers')"
      />
      <MetricCard
        label="Processing Rate"
        :value="throughput.current"
        format="raw"
        subtext="messages/sec"
        :trend="throughput.trend"
        :loading="loadingStatus"
      />
    </div>

    <!-- Health Indicators Row (Compact) -->
    <div class="grid grid-cols-2 lg:grid-cols-4 gap-2 sm:gap-3">
      <!-- Time Lag -->
      <div class="card px-3 py-2">
        <div class="flex items-center justify-between">
          <div class="flex items-center gap-1.5">
            <svg class="w-3.5 h-3.5 text-amber-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <span class="text-xs font-medium text-light-500">Time Lag</span>
          </div>
          <div class="flex items-center gap-3 text-xs">
            <span :class="timeLagClass(overview?.lag?.time?.avg)" class="font-semibold tabular-nums">
              {{ formatDuration(overview?.lag?.time?.avg || 0) }}
            </span>
            <span class="text-light-400">/</span>
            <span :class="timeLagClass(overview?.lag?.time?.max)" class="font-semibold tabular-nums">
              {{ formatDuration(overview?.lag?.time?.max || 0) }}
            </span>
          </div>
        </div>
      </div>

      <!-- Offset Lag -->
      <div class="card px-3 py-2">
        <div class="flex items-center justify-between">
          <div class="flex items-center gap-1.5">
            <svg class="w-3.5 h-3.5 text-blue-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2" />
            </svg>
            <span class="text-xs font-medium text-light-500">Pending</span>
          </div>
          <div class="flex items-center gap-3 text-xs">
            <span :class="offsetLagClass(overview?.lag?.offset?.avg)" class="font-semibold tabular-nums">
              {{ formatNumber(overview?.lag?.offset?.avg || 0) }}
            </span>
            <span class="text-light-400">/</span>
            <span :class="offsetLagClass(overview?.lag?.offset?.max)" class="font-semibold tabular-nums">
              {{ formatNumber(overview?.lag?.offset?.max || 0) }}
            </span>
          </div>
        </div>
      </div>

      <!-- Event Loop Lag -->
      <div class="card px-3 py-2">
        <div class="flex items-center justify-between">
          <div class="flex items-center gap-1.5">
            <svg class="w-3.5 h-3.5 text-rose-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z" />
            </svg>
            <span class="text-xs font-medium text-light-500">Event Loop</span>
          </div>
          <div class="flex items-center gap-3 text-xs">
            <span :class="eventLoopClass(avgEventLoopLag)" class="font-semibold tabular-nums">
              {{ avgEventLoopLag }}ms
            </span>
            <span class="text-light-400">/</span>
            <span :class="eventLoopClass(maxEventLoopLag)" class="font-semibold tabular-nums">
              {{ maxEventLoopLag }}ms
            </span>
          </div>
        </div>
      </div>

      <!-- Batch Efficiency -->
      <div class="card px-3 py-2">
        <div class="flex items-center justify-between">
          <div class="flex items-center gap-1.5">
            <svg class="w-3.5 h-3.5 text-emerald-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10" />
            </svg>
            <span class="text-xs font-medium text-light-500">Batch</span>
          </div>
          <div class="flex items-center gap-2 text-xs">
            <span class="font-semibold text-emerald-600 dark:text-emerald-400 tabular-nums">{{ batchEfficiency.push }}</span>
            <span class="font-semibold text-indigo-600 dark:text-indigo-400 tabular-nums">{{ batchEfficiency.pop }}</span>
            <span class="font-semibold text-green-600 dark:text-green-400 tabular-nums">{{ batchEfficiency.ack }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Charts row -->
    <div class="grid grid-cols-1 lg:grid-cols-3 gap-3 sm:gap-5">
      <!-- Throughput chart -->
      <div class="lg:col-span-2 card">
        <div class="card-header flex flex-col sm:flex-row sm:items-center sm:justify-between gap-2">
          <div>
            <h3 class="font-semibold text-light-900 dark:text-white">Throughput</h3>
            <p class="text-xs text-light-500 mt-0.5">Messages per second</p>
          </div>
          <div class="flex items-center gap-1 sm:gap-2">
            <button 
              v-for="range in timeRanges" 
              :key="range.value"
              @click="selectedRange = range.value"
              class="px-2 sm:px-3 py-1 sm:py-1.5 text-xs font-medium rounded-md transition-colors"
              :class="selectedRange === range.value 
                ? 'bg-queen-500 text-white' 
                : 'text-light-600 dark:text-light-400 hover:bg-light-200 dark:hover:bg-dark-100'"
            >
              {{ range.label }}
            </button>
          </div>
        </div>
        <div class="card-body">
          <BaseChart 
            v-if="chartData.labels.length > 0"
            type="line" 
            :data="chartData"
            :options="messageFlowChartOptions"
            height="280px"
          />
          <div v-else class="h-[280px] flex items-center justify-center">
            <div class="text-center text-light-500">
              <div class="spinner mx-auto mb-3" />
              <p class="text-sm">Loading chart data...</p>
            </div>
          </div>
        </div>
      </div>

      <!-- Queue time lag -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Queue Time Lag</h3>
          <p class="text-xs text-light-500 mt-0.5">Max consumer lag per queue</p>
        </div>
        <div class="card-body">
          <div v-if="queueTimeLag.length > 0" class="space-y-4">
            <div 
              v-for="queue in queueTimeLag.slice(0, 5)" 
              :key="queue.name"
              class="group"
            >
              <div class="flex items-center justify-between text-sm mb-1.5">
                <span class="font-medium text-light-800 dark:text-light-200 truncate max-w-[150px]">
                  {{ queue.name }}
                </span>
                <span 
                  class="tabular-nums font-medium"
                  :class="queue.lag > 600 ? 'text-rose-600 dark:text-rose-400' : 
                          queue.lag > 60 ? 'text-amber-600 dark:text-amber-400' : 
                          'text-emerald-600 dark:text-emerald-400'"
                >
                  {{ queue.lag > 0 ? formatDuration(queue.lag) : '-' }}
                </span>
              </div>
              <div class="h-2 bg-light-200 dark:bg-dark-100 rounded-full overflow-hidden">
                <div 
                  class="h-full rounded-full transition-all duration-500 group-hover:opacity-80"
                  :class="queue.colorClass"
                  :style="{ width: `${queue.percentage}%` }"
                />
              </div>
            </div>
            <router-link 
              v-if="queueTimeLag.length > 5"
              to="/consumers" 
              class="inline-flex items-center gap-1 text-sm text-queen-600 dark:text-queen-400 hover:underline mt-2"
            >
              View all {{ queueTimeLag.length }} queues
              <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
            </router-link>
          </div>
          <div v-else class="h-[200px] flex items-center justify-center text-light-500 text-sm">
            No consumer groups found
          </div>
        </div>
      </div>
    </div>

    <!-- Bottom row -->
    <div class="grid grid-cols-1 lg:grid-cols-2 gap-3 sm:gap-5">
      <!-- Recent queues table -->
      <DataTable
        title="Active Queues"
        subtitle="Data may be stale"
        :columns="queueColumns"
        :data="sortedQueues"
        :loading="loadingQueues"
        :page-size="5"
        clickable
        @row-click="(row) => $router.push(`/queues/${row.name}`)"
      >
        <template #name="{ value }">
          <span class="font-medium text-light-900 dark:text-white">{{ value }}</span>
        </template>
        <template #messages.pending="{ row }">
          <span class="badge badge-cyber">{{ formatNumber(row.messages?.pending || 0) }}</span>
        </template>
      </DataTable>

      <!-- Consumer groups -->
      <DataTable
        title="Consumer Groups"
        subtitle="Time lag data is accurate and updated in real-time"
        :columns="consumerColumns"
        :data="sortedConsumers"
        :loading="loadingConsumers"
        :page-size="5"
        clickable
        @row-click="(row) => $router.push('/consumers')"
      >
        <template #name="{ value, row }">
          <div class="truncate max-w-[180px]">
            <span class="font-medium text-light-900 dark:text-white">{{ value }}</span>
            <p v-if="row.queueName" class="text-xs text-light-500 dark:text-light-400 truncate">{{ row.queueName }}</p>
          </div>
        </template>
        <template #members="{ value }">
          <span class="badge badge-queen">{{ value }}</span>
        </template>
        <template #maxTimeLag="{ value }">
          <span 
            class="badge"
            :class="value > 300 ? 'badge-danger' : value > 60 ? 'badge-warning' : 'badge-success'"
          >
            {{ value > 0 ? formatDuration(value) : '-' }}
          </span>
        </template>
      </DataTable>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch, h } from 'vue'
import { resources, queues as queuesApi, analytics, consumers as consumersApi } from '@/api'
import { formatNumber } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'
import MetricCard from '@/components/MetricCard.vue'
import BaseChart from '@/components/BaseChart.vue'
import DataTable from '@/components/DataTable.vue'

// State
const overview = ref(null)
const queues = ref([])
const consumers = ref([])
const statusData = ref(null)
const lastUpdated = ref(new Date())

const loadingOverview = ref(true)
const loadingQueues = ref(true)
const loadingConsumers = ref(true)
const loadingStatus = ref(true)

// Chart state
const selectedRange = ref('1h')
const timeRanges = [
  { label: '1h', value: '1h', minutes: 60 },
  { label: '6h', value: '6h', minutes: 360 },
  { label: '24h', value: '24h', minutes: 1440 },
]

// Get time range params for API call
const getTimeRangeParams = () => {
  const range = timeRanges.find(r => r.value === selectedRange.value) || timeRanges[0]
  const now = new Date()
  const from = new Date(now.getTime() - range.minutes * 60 * 1000)
  return {
    from: from.toISOString(),
    to: now.toISOString()
  }
}

// Total partitions from queues
const totalPartitions = computed(() => 
  queues.value.reduce((sum, q) => sum + (q.partitions || 1), 0)
)

// Throughput calculation - using overview.throughput and statusData for trend
const throughput = computed(() => {
  if (!overview.value?.throughput) {
    return { current: '0', trend: 0 }
  }
  
  const current = overview.value.throughput.ingestedPerSecond || overview.value.throughput.processedPerSecond || 0
  
  // Calculate trend: compare current rate to previous rate
  // Use ingestedPerSecond which is already a rate
  let trend = 0
  if (statusData.value?.throughput && statusData.value.throughput.length >= 2) {
    const history = statusData.value.throughput
    // history[0] is most recent, history[length-1] is oldest
    
    // Get current rate (most recent entry)
    const currentRate = history[0]?.ingestedPerSecond || 0
    
    // Get previous rate (average of entries from 5+ minutes ago, or last few entries if not enough data)
    const olderEntries = history.length > 5 ? history.slice(5) : history.slice(1)
    const previousRate = olderEntries.length > 0
      ? olderEntries.reduce((sum, h) => sum + (h.ingestedPerSecond || 0), 0) / olderEntries.length
      : 0
    
    // Calculate percentage change
    if (previousRate > 0 && currentRate > 0) {
      trend = Math.round(((currentRate - previousRate) / previousRate) * 100)
    } else if (previousRate === 0 && currentRate > 0) {
      trend = 100 // Activity started
    } else if (previousRate > 0 && currentRate === 0) {
      trend = -100 // Activity stopped
    }
    // If both are 0, trend stays 0
  }
  
  return {
    current: current.toFixed(1),
    trend
  }
})

// Event loop lag from workers
const avgEventLoopLag = computed(() => {
  const workers = statusData.value?.workers
  if (!workers || workers.length === 0) return 0
  return Math.round(workers.reduce((sum, w) => sum + (w.avgEventLoopLagMs || 0), 0) / workers.length)
})

const maxEventLoopLag = computed(() => {
  const workers = statusData.value?.workers
  if (!workers || workers.length === 0) return 0
  return Math.max(...workers.map(w => w.maxEventLoopLagMs || 0))
})

// Batch efficiency from statusData
const batchEfficiency = computed(() => {
  const be = statusData.value?.messages?.batchEfficiency
  return {
    push: be?.push?.toFixed(1) || '0',
    pop: be?.pop?.toFixed(1) || '0',
    ack: be?.ack?.toFixed(1) || '0'
  }
})

// Format duration in seconds to human-readable string
const formatDuration = (seconds) => {
  if (!seconds || seconds === 0) return '0s'
  if (seconds < 60) return `${Math.round(seconds)}s`
  if (seconds < 3600) {
    const mins = Math.floor(seconds / 60)
    const secs = Math.round(seconds % 60)
    return secs > 0 ? `${mins}m ${secs}s` : `${mins}m`
  }
  if (seconds < 86400) {
    const hours = Math.floor(seconds / 3600)
    const mins = Math.floor((seconds % 3600) / 60)
    return mins > 0 ? `${hours}h ${mins}m` : `${hours}h`
  }
  const days = Math.floor(seconds / 86400)
  const hours = Math.floor((seconds % 86400) / 3600)
  return hours > 0 ? `${days}d ${hours}h` : `${days}d`
}

// Color classes for health indicators
const timeLagClass = (seconds) => {
  if (!seconds || seconds === 0) return 'text-light-600 dark:text-light-300'
  if (seconds < 60) return 'text-emerald-600 dark:text-emerald-400'
  if (seconds < 300) return 'text-amber-600 dark:text-amber-400'
  return 'text-rose-600 dark:text-rose-400'
}

const offsetLagClass = (count) => {
  if (!count || count === 0) return 'text-light-600 dark:text-light-300'
  if (count < 10) return 'text-emerald-600 dark:text-emerald-400'
  if (count < 50) return 'text-amber-600 dark:text-amber-400'
  return 'text-rose-600 dark:text-rose-400'
}

const eventLoopClass = (ms) => {
  if (!ms || ms === 0) return 'text-light-600 dark:text-light-300'
  if (ms < 50) return 'text-emerald-600 dark:text-emerald-400'
  if (ms < 100) return 'text-amber-600 dark:text-amber-400'
  return 'text-rose-600 dark:text-rose-400'
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

// Chart data - using statusData.throughput array (showing msg/s)
const chartData = computed(() => {
  if (!statusData.value?.throughput || !Array.isArray(statusData.value.throughput)) {
    return { labels: [], datasets: [] }
  }
  
  // Reverse to get chronological order (oldest first)
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
        label: 'In (msg/s)',
        data: history.map(h => h.ingestedPerSecond || 0),
        fill: true,
      },
      {
        label: 'Out (msg/s)',
        data: history.map(h => h.processedPerSecond || 0),
        fill: true,
      }
    ]
  }
})

// Chart options with Y-axis title
const messageFlowChartOptions = {
  plugins: { legend: { display: false } },
  scales: {
    y: {
      title: { display: true, text: 'msg/s', font: { size: 11 } }
    }
  }
}

// Queue time lag - group consumers by queue and show max time lag
const queueTimeLag = computed(() => {
  if (!consumers.value.length) return []
  
  // Group consumers by queue and find max time lag per queue
  const queueLagMap = {}
  consumers.value.forEach(c => {
    const queueName = c.queueName || 'Unknown'
    const lag = c.maxTimeLag || 0
    if (!queueLagMap[queueName] || lag > queueLagMap[queueName]) {
      queueLagMap[queueName] = lag
    }
  })
  
  const queueLags = Object.entries(queueLagMap).map(([name, lag]) => ({ name, lag }))
  const maxLag = Math.max(...queueLags.map(q => q.lag), 1) // Avoid division by zero
  
  const colors = [
    'bg-gradient-to-r from-rose-500 to-rose-600',
    'bg-gradient-to-r from-amber-500 to-amber-600',
    'bg-gradient-to-r from-orange-500 to-orange-600',
    'bg-gradient-to-r from-red-500 to-red-600',
    'bg-gradient-to-r from-pink-500 to-pink-600',
  ]
  
  return queueLags
    .sort((a, b) => b.lag - a.lag)
    .map((q, i) => ({
      name: q.name,
      lag: q.lag,
      percentage: maxLag > 0 ? (q.lag / maxLag) * 100 : 0,
      colorClass: q.lag > 600 ? 'bg-gradient-to-r from-rose-500 to-rose-600' : 
                  q.lag > 60 ? 'bg-gradient-to-r from-amber-500 to-amber-600' : 
                  'bg-gradient-to-r from-emerald-500 to-emerald-600'
    }))
})

// Table columns
const queueColumns = [
  { key: 'name', label: 'Queue Name', sortable: true },
  { key: 'messages.pending', label: 'Pending', sortable: true, align: 'right' },
]

const consumerColumns = [
  { key: 'name', label: 'Group Name', sortable: true },
  { key: 'members', label: 'Partitions', sortable: true, align: 'right' },
  { key: 'maxTimeLag', label: 'Time Lag', sortable: true, align: 'right' },
]

// Sorted data for tables
const sortedQueues = computed(() => {
  return [...queues.value].sort((a, b) => {
    const pendingA = a.messages?.pending || 0
    const pendingB = b.messages?.pending || 0
    return pendingB - pendingA // Descending by pending
  })
})

const sortedConsumers = computed(() => {
  return [...consumers.value].sort((a, b) => {
    const lagA = a.maxTimeLag || 0
    const lagB = b.maxTimeLag || 0
    return lagB - lagA // Descending by time lag
  })
})

// Fetch data - only show loading state on initial load, not on background refresh
const fetchOverview = async () => {
  // Only show loading skeleton if we don't have data yet
  if (!overview.value) loadingOverview.value = true
  try {
    const response = await resources.getOverview()
    overview.value = response.data
  } catch (err) {
    console.error('Failed to fetch overview:', err)
  } finally {
    loadingOverview.value = false
  }
}

const fetchQueues = async () => {
  // Only show loading skeleton if we don't have data yet
  if (!queues.value.length) loadingQueues.value = true
  try {
    const response = await queuesApi.list()
    queues.value = response.data?.queues || response.data || []
  } catch (err) {
    console.error('Failed to fetch queues:', err)
  } finally {
    loadingQueues.value = false
  }
}

const fetchConsumers = async () => {
  // Only show loading skeleton if we don't have data yet
  if (!consumers.value.length) loadingConsumers.value = true
  try {
    const response = await consumersApi.list()
    // API returns array directly, not nested in consumer_groups
    consumers.value = Array.isArray(response.data) ? response.data : response.data?.consumer_groups || []
  } catch (err) {
    console.error('Failed to fetch consumers:', err)
  } finally {
    loadingConsumers.value = false
  }
}

const fetchStatus = async () => {
  // Only show loading skeleton if we don't have data yet
  if (!statusData.value) loadingStatus.value = true
  try {
    const params = getTimeRangeParams()
    const response = await analytics.getStatus(params)
    statusData.value = response.data
  } catch (err) {
    console.error('Failed to fetch status:', err)
  } finally {
    loadingStatus.value = false
  }
}

const fetchAll = async () => {
  lastUpdated.value = new Date()
  await Promise.all([
    fetchOverview(),
    fetchQueues(),
    fetchConsumers(),
    fetchStatus(),
  ])
}

// Register for global refresh
useRefresh(fetchAll)

// Watch for time range changes
watch(selectedRange, () => {
  fetchStatus()
})

// Auto-refresh
let refreshInterval = null

onMounted(() => {
  fetchAll()
  refreshInterval = setInterval(fetchAll, 30000) // Refresh every 30s
})

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})

// Icons
function QueuesIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M3.75 12h16.5m-16.5 3.75h16.5M3.75 19.5h16.5M5.625 4.5h12.75a1.875 1.875 0 010 3.75H5.625a1.875 1.875 0 010-3.75z' })
  ])
}

function MessagesIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M21.75 6.75v10.5a2.25 2.25 0 01-2.25 2.25h-15a2.25 2.25 0 01-2.25-2.25V6.75m19.5 0A2.25 2.25 0 0019.5 4.5h-15a2.25 2.25 0 00-2.25 2.25m19.5 0v.243a2.25 2.25 0 01-1.07 1.916l-7.5 4.615a2.25 2.25 0 01-2.36 0L3.32 8.91a2.25 2.25 0 01-1.07-1.916V6.75' })
  ])
}

function ConsumersIcon(props) {
  return h('svg', { ...props, fill: 'none', viewBox: '0 0 24 24', stroke: 'currentColor', 'stroke-width': '1.5' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', d: 'M18 18.72a9.094 9.094 0 003.741-.479 3 3 0 00-4.682-2.72m.94 3.198l.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0112 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 016 18.719m12 0a5.971 5.971 0 00-.941-3.197m0 0A5.995 5.995 0 0012 12.75a5.995 5.995 0 00-5.058 2.772m0 0a3 3 0 00-4.681 2.72 8.986 8.986 0 003.74.477m.94-3.197a5.971 5.971 0 00-.94 3.197M15 6.75a3 3 0 11-6 0 3 3 0 016 0zm6 3a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0zm-13.5 0a2.25 2.25 0 11-4.5 0 2.25 2.25 0 014.5 0z' })
  ])
}
</script>

