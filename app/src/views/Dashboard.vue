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

      <!-- Queue distribution -->
      <div class="card">
        <div class="card-header">
          <h3 class="font-semibold text-light-900 dark:text-white">Queue Distribution</h3>
          <p class="text-xs text-light-500 mt-0.5">Pending messages per queue</p>
        </div>
        <div class="card-body">
          <div v-if="queueDistribution.length > 0" class="space-y-4">
            <div 
              v-for="queue in queueDistribution.slice(0, 5)" 
              :key="queue.name"
              class="group"
            >
              <div class="flex items-center justify-between text-sm mb-1.5">
                <span class="font-medium text-light-800 dark:text-light-200 truncate max-w-[150px]">
                  {{ queue.name }}
                </span>
                <span class="text-light-600 dark:text-light-400 tabular-nums">
                  {{ formatNumber(queue.count) }}
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
              v-if="queueDistribution.length > 5"
              to="/queues" 
              class="inline-flex items-center gap-1 text-sm text-queen-600 dark:text-queen-400 hover:underline mt-2"
            >
              View all {{ queueDistribution.length }} queues
              <svg class="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
            </router-link>
          </div>
          <div v-else class="h-[200px] flex items-center justify-center text-light-500 text-sm">
            No queues found
          </div>
        </div>
      </div>
    </div>

    <!-- Bottom row -->
    <div class="grid grid-cols-1 lg:grid-cols-2 gap-3 sm:gap-5">
      <!-- Recent queues table -->
      <DataTable
        title="Active Queues"
        :columns="queueColumns"
        :data="queues"
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
        <template #messages.processing="{ row }">
          <span class="badge badge-crown">{{ formatNumber(row.messages?.processing || 0) }}</span>
        </template>
        <template #status="{ row }">
          <span 
            class="inline-flex items-center gap-1.5"
            :class="(row.messages?.processing || 0) > 0 ? 'text-emerald-600 dark:text-emerald-400' : 'text-light-500'"
          >
            <span 
              class="w-1.5 h-1.5 rounded-full"
              :class="(row.messages?.processing || 0) > 0 ? 'bg-emerald-500 animate-pulse' : 'bg-light-400'"
            />
            {{ (row.messages?.processing || 0) > 0 ? 'Active' : 'Idle' }}
          </span>
        </template>
      </DataTable>

      <!-- Consumer groups -->
      <DataTable
        title="Consumer Groups"
        :columns="consumerColumns"
        :data="consumers"
        :loading="loadingConsumers"
        :page-size="5"
        clickable
        @row-click="(row) => $router.push('/consumers')"
      >
        <template #name="{ value }">
          <span class="font-medium text-light-900 dark:text-white truncate max-w-[180px]">{{ value }}</span>
        </template>
        <template #members="{ value }">
          <span class="badge badge-queen">{{ value }}</span>
        </template>
        <template #totalLag="{ value }">
          <span 
            class="badge"
            :class="value > 1000 ? 'badge-danger' : value > 100 ? 'badge-warning' : 'badge-success'"
          >
            {{ formatNumber(value || 0) }}
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

// Queue distribution - using messages.pending and messages.processing
const queueDistribution = computed(() => {
  if (!queues.value.length) return []
  
  const total = queues.value.reduce((sum, q) => {
    const pending = q.messages?.pending || 0
    const processing = q.messages?.processing || 0
    return sum + pending + processing
  }, 0)
  
  const colors = [
    'bg-gradient-to-r from-queen-500 to-queen-600',
    'bg-gradient-to-r from-cyber-500 to-cyber-600',
    'bg-gradient-to-r from-crown-500 to-crown-600',
    'bg-gradient-to-r from-emerald-500 to-emerald-600',
    'bg-gradient-to-r from-violet-500 to-violet-600',
  ]
  
  return queues.value
    .map((q, i) => {
      const pending = q.messages?.pending || 0
      const processing = q.messages?.processing || 0
      const count = pending + processing
      return {
        name: q.name,
        count,
        percentage: total > 0 ? (count / total) * 100 : 0,
        colorClass: colors[i % colors.length]
      }
    })
    .sort((a, b) => b.count - a.count)
})

// Table columns
const queueColumns = [
  { key: 'name', label: 'Queue Name', sortable: true },
  { key: 'messages.pending', label: 'Pending', sortable: true, align: 'right' },
  { key: 'messages.processing', label: 'Processing', sortable: true, align: 'right' },
  { key: 'status', label: 'Status' },
]

const consumerColumns = [
  { key: 'name', label: 'Group Name', sortable: true },
  { key: 'members', label: 'Members', sortable: true, align: 'right' },
  { key: 'totalLag', label: 'Lag', sortable: true, align: 'right' },
]

// Fetch data
const fetchOverview = async () => {
  loadingOverview.value = true
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
  loadingQueues.value = true
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
  loadingConsumers.value = true
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
  loadingStatus.value = true
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

