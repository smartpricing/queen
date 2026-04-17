<template>
  <div class="view-container animate-fade-in">

    <!-- Page head -->
    <div class="page-head">
      <div>
        <div class="eyebrow">Insights</div>
        <h1><span class="accent">Analytics</span></h1>
        <p>Throughput, distribution and health metrics for your message pipeline.</p>
      </div>
    </div>

    <!-- Filters card -->
    <div class="card" style="margin-bottom:20px;">
      <div class="card-body" style="display:flex; flex-direction:column; gap:16px;">

        <!-- Time range row -->
        <div style="display:flex; flex-wrap:wrap; align-items:center; gap:12px;">
          <span class="label-xs">Time range</span>
          <div class="seg">
            <button
              v-for="range in timeRanges"
              :key="range.value"
              :class="{ on: timeRange === range.value && !customMode }"
              @click="selectQuickRange(range.value)"
            >{{ range.label }}</button>
            <button
              :class="{ on: customMode }"
              @click="toggleCustomMode"
            >Custom</button>
          </div>
        </div>

        <!-- Custom date/time range -->
        <div v-if="customMode" style="display:flex; flex-wrap:wrap; align-items:center; gap:12px; padding-top:12px; border-top:1px solid var(--bd);">
          <div style="display:flex; align-items:center; gap:8px;">
            <span class="label-xs">From</span>
            <input
              type="datetime-local"
              v-model="customFrom"
              class="input font-mono"
              style="width:auto; font-size:13px;"
            />
          </div>
          <div style="display:flex; align-items:center; gap:8px;">
            <span class="label-xs">To</span>
            <input
              type="datetime-local"
              v-model="customTo"
              class="input font-mono"
              style="width:auto; font-size:13px;"
            />
          </div>
          <button
            @click="applyCustomRange"
            class="btn btn-primary"
          >Apply</button>
        </div>

        <!-- Filters row -->
        <div style="display:flex; flex-wrap:wrap; align-items:flex-end; gap:12px;">
          <div style="width:192px;">
            <span class="label-xs" style="display:block; margin-bottom:6px;">Queue</span>
            <select v-model="queueFilter" class="input" style="width:100%;">
              <option value="">All Queues</option>
              <option v-for="q in allQueues" :key="q.name" :value="q.name">
                {{ q.name }}
              </option>
            </select>
          </div>

          <div style="width:160px;">
            <span class="label-xs" style="display:block; margin-bottom:6px;">Namespace</span>
            <select v-model="namespaceFilter" class="input" style="width:100%;">
              <option value="">All Namespaces</option>
              <option v-for="ns in namespaces" :key="ns.namespace" :value="ns.namespace">
                {{ ns.namespace || 'Default' }}
              </option>
            </select>
          </div>

          <div style="width:160px;">
            <span class="label-xs" style="display:block; margin-bottom:6px;">Task</span>
            <select v-model="taskFilter" class="input" style="width:100%;">
              <option value="">All Tasks</option>
              <option v-for="task in tasks" :key="task.task" :value="task.task">
                {{ task.task || 'Default' }}
              </option>
            </select>
          </div>

          <button
            v-if="queueFilter || namespaceFilter || taskFilter"
            @click="clearFilters"
            class="btn btn-ghost"
          >Clear Filters</button>
        </div>
      </div>

      <!-- Active filters display -->
      <div v-if="queueFilter || namespaceFilter || taskFilter" style="display:flex; flex-wrap:wrap; gap:8px; padding:0 16px 14px; border-top:1px solid var(--bd); padding-top:12px;">
        <span v-if="queueFilter" class="chip chip-ice">
          Queue: {{ queueFilter }}
        </span>
        <span v-if="namespaceFilter" class="chip chip-ice">
          Namespace: {{ namespaceFilter }}
        </span>
        <span v-if="taskFilter" class="chip chip-warn">
          Task: {{ taskFilter }}
        </span>
      </div>
    </div>

    <!-- Loading skeleton -->
    <div v-if="loading" style="display:flex; flex-direction:column; gap:16px;">
      <div class="card" style="padding:24px;">
        <div class="skeleton" style="height:256px; width:100%; border-radius:10px;" />
      </div>
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:16px;">
        <div class="card" style="padding:24px;">
          <div class="skeleton" style="height:192px; width:100%; border-radius:10px;" />
        </div>
        <div class="card" style="padding:24px;">
          <div class="skeleton" style="height:192px; width:100%; border-radius:10px;" />
        </div>
      </div>
    </div>

    <template v-else-if="statusData">
      <!-- Message Flow Chart -->
      <div class="card card-accent" style="margin-bottom:20px;">
        <div class="card-header">
          <h3>Message Flow Over Time</h3>
        </div>
        <div class="card-body">
          <BaseChart
            v-if="throughputData.labels.length > 0"
            type="line"
            :data="throughputData"
            :options="chartOptions"
            height="280px"
          />
          <div v-else style="text-align:center; padding:48px 0; color:var(--text-mid);">
            No throughput data available
          </div>
        </div>
      </div>

      <!-- Charts row -->
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:16px; margin-bottom:20px;">
        <!-- Top Queues Chart -->
        <div class="card">
          <div class="card-header">
            <h3>Top Queues by Volume</h3>
          </div>
          <div class="card-body">
            <BaseChart
              v-if="queueActivityData.labels.length > 0"
              type="bar"
              :data="queueActivityData"
              :options="barChartOptions"
              height="240px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-mid);">
              No queue data available
            </div>
          </div>
        </div>

        <!-- Message Distribution Chart -->
        <div class="card">
          <div class="card-header">
            <h3>Message Status Distribution</h3>
          </div>
          <div class="card-body" style="display:flex; align-items:center; justify-content:center;">
            <BaseChart
              v-if="messageDistributionData.labels.length > 0"
              type="doughnut"
              :data="messageDistributionData"
              :options="doughnutOptions"
              height="240px"
            />
            <div v-else style="text-align:center; padding:48px 0; color:var(--text-mid);">
              No message data available
            </div>
          </div>
        </div>
      </div>

      <!-- Message Counts -->
      <div class="card" style="margin-bottom:20px;">
        <div class="card-header">
          <h3>Message Counts</h3>
        </div>
        <div class="card-body">
          <div style="display:grid; grid-template-columns:repeat(5,1fr); gap:16px;">
            <div class="stat">
              <div class="stat-label">Pending</div>
              <div class="stat-value font-mono" style="color:#22d3ee;">
                {{ formatNumber(Math.max(0, statusData?.messages?.pending || 0)) }}
              </div>
            </div>
            <div class="stat">
              <div class="stat-label">Processing</div>
              <div class="stat-value font-mono" style="color:#fbbf24;">
                {{ formatNumber(statusData?.messages?.processing || 0) }}
              </div>
            </div>
            <div class="stat">
              <div class="stat-label">Completed</div>
              <div class="stat-value font-mono" style="color:#34d399;">
                {{ formatNumber(statusData?.messages?.completed || 0) }}
              </div>
            </div>
            <div class="stat">
              <div class="stat-label">Failed</div>
              <div class="stat-value font-mono" style="color:#fb923c;">
                {{ formatNumber(statusData?.messages?.failed || 0) }}
              </div>
            </div>
            <div class="stat">
              <div class="stat-label">Dead Letter</div>
              <div class="stat-value font-mono" style="color:#f43f5e;">
                {{ formatNumber(statusData?.messages?.deadLetter || 0) }}
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Detailed Stats -->
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:16px; margin-bottom:20px;">
        <!-- Leases Info -->
        <div class="card">
          <div class="card-header">
            <h3>Active Leases</h3>
          </div>
          <div class="card-body" style="display:flex; flex-direction:column; gap:12px;">
            <div style="display:flex; align-items:center; justify-content:space-between;">
              <span style="color:var(--text-mid); font-size:13px;">Active Leases</span>
              <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi);">{{ statusData?.leases?.active || 0 }}</span>
            </div>
            <div style="display:flex; align-items:center; justify-content:space-between;">
              <span style="color:var(--text-mid); font-size:13px;">Partitions with Leases</span>
              <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi);">{{ statusData?.leases?.partitionsWithLeases || 0 }}</span>
            </div>
            <div style="display:flex; align-items:center; justify-content:space-between;">
              <span style="color:var(--text-mid); font-size:13px;">Total Batch Size</span>
              <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi);">{{ statusData?.leases?.totalBatchSize || 0 }}</span>
            </div>
            <div style="display:flex; align-items:center; justify-content:space-between;">
              <span style="color:var(--text-mid); font-size:13px;">Total Acked</span>
              <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi);">{{ statusData?.leases?.totalAcked || 0 }}</span>
            </div>
          </div>
        </div>

        <!-- Dead Letter Queue -->
        <div class="card">
          <div class="card-header">
            <h3>Dead Letter Queue</h3>
          </div>
          <div class="card-body" style="display:flex; flex-direction:column; gap:12px;">
            <div style="display:flex; align-items:center; justify-content:space-between;">
              <span style="color:var(--text-mid); font-size:13px;">Total Messages</span>
              <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi);">{{ statusData?.deadLetterQueue?.totalMessages || 0 }}</span>
            </div>
            <div style="display:flex; align-items:center; justify-content:space-between;">
              <span style="color:var(--text-mid); font-size:13px;">Affected Partitions</span>
              <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi);">{{ statusData?.deadLetterQueue?.affectedPartitions || 0 }}</span>
            </div>

            <div v-if="statusData?.deadLetterQueue?.topErrors?.length" style="padding-top:12px; border-top:1px solid var(--bd);">
              <span class="label-xs" style="display:block; margin-bottom:8px;">Top Errors</span>
              <div style="display:flex; flex-direction:column; gap:8px;">
                <div
                  v-for="(errorItem, idx) in statusData.deadLetterQueue.topErrors.slice(0, 3)"
                  :key="idx"
                  style="display:flex; align-items:center; justify-content:space-between; font-size:13px;"
                >
                  <span style="color:var(--text-mid); overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex:1;">{{ errorItem.error }}</span>
                  <span class="font-mono tabular-nums" style="font-weight:600; color:var(--text-hi); margin-left:8px;">{{ errorItem.count }}</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Workers Info -->
      <div v-if="statusData?.workers?.length" class="card">
        <div class="card-header">
          <h3>{{ statusData.workers.length }} Workers</h3>
          <span class="chip chip-ok"><span class="dot"></span>Healthy</span>
        </div>
        <div class="card-body">
          <div class="grid-3">
            <div class="stat" style="text-align:center;">
              <div class="stat-label" style="justify-content:center;">Avg Event Loop</div>
              <div class="stat-value font-mono">
                {{ Math.round(statusData.workers.reduce((sum, w) => sum + (w.avgEventLoopLagMs || 0), 0) / statusData.workers.length) }}<small>ms</small>
              </div>
            </div>
            <div class="stat" style="text-align:center;">
              <div class="stat-label" style="justify-content:center;">Connection Pool</div>
              <div class="stat-value font-mono">
                {{ statusData.workers.reduce((sum, w) => sum + (w.freeSlots || 0), 0) }}<small>/{{ statusData.workers.reduce((sum, w) => sum + (w.dbConnections || 0), 0) }}</small>
              </div>
            </div>
            <div class="stat" style="text-align:center;">
              <div class="stat-label" style="justify-content:center;">Max Job Queue</div>
              <div class="stat-value font-mono">
                {{ Math.max(...statusData.workers.map(w => w.jobQueueSize || 0)) }}
              </div>
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
    { label: 'Pending', value: Math.max(0, data.pending || 0), color: 'rgba(6, 182, 212, 0.8)' },
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
