<template>
  <div class="dashboard">
    <div class="dashboard-header">
      <h1>System Overview</h1>
      <div class="header-actions">
        <Button 
          label="Refresh" 
          icon="pi pi-refresh" 
          @click="refreshData"
          :loading="loading"
        />
      </div>
    </div>


    <!-- Metric Cards -->
    <div class="metrics-grid">
      <MetricCard 
        title="Total Messages"
        :value="formatNumber(metrics.totalMessages)"
        icon="pi pi-envelope"
        :trend="metrics.messageTrend"
        color="primary"
        :loading="loading"
      />
      <MetricCard 
        title="Pending"
        :value="formatNumber(metrics.pending)"
        icon="pi pi-clock"
        color="warning"
        :loading="loading"
      />
      <MetricCard 
        title="Processing"
        :value="formatNumber(metrics.processing)"
        icon="pi pi-spin pi-spinner"
        color="info"
        :loading="loading"
      />
      <MetricCard 
        title="Completed Today"
        :value="formatNumber(metrics.completedToday)"
        icon="pi pi-check-circle"
        color="success"
        :loading="loading"
      />
      <MetricCard 
        title="Failed Today"
        :value="formatNumber(metrics.failedToday)"
        icon="pi pi-times-circle"
        color="danger"
        :loading="loading"
      />
      <MetricCard 
        title="Messages/sec"
        :value="metrics.messagesPerSecond"
        icon="pi pi-bolt"
        color="primary"
        suffix="/s"
        :loading="loading"
      />
    </div>

    <!-- Charts Row -->
    <div class="charts-row">
      <Card class="chart-card">
        <template #title>
          <div class="card-header-custom">
            <span>Throughput (Last Hour)</span>
            <Tag :value="`${currentThroughput} msg/min`" severity="info" />
          </div>
        </template>
        <template #content>
          <ThroughputChart :data="throughputData" :loading="loading" />
        </template>
      </Card>

      <Card class="chart-card">
        <template #title>
          <div class="card-header-custom">
            <span>Queue Depths</span>
            <Tag :value="`${activeQueues} active`" severity="info" />
          </div>
        </template>
        <template #content>
          <QueueDepthChart :data="queueDepthData" :loading="loading" />
        </template>
      </Card>
    </div>

    <!-- Queue List and Activity Feed -->
    <div class="bottom-row">
      <Card class="queue-list-card">
        <template #title>
          <div class="card-header-custom">
            <span>Top Queues by Activity</span>
            <Button 
              label="View All" 
              class="p-button-text p-button-sm"
              @click="$router.push('/queues')"
            />
          </div>
        </template>
        <template #content>
          <DataTable 
          :value="topQueues" 
          :loading="loading"
          responsiveLayout="scroll"
          :rows="5"
        >
          <Column field="name" header="Queue" />
          <Column field="namespace" header="Namespace">
            <template #body="{ data }">
              <Tag v-if="data.namespace" :value="data.namespace" severity="secondary" />
              <span v-else class="text-muted">-</span>
            </template>
          </Column>
          <Column field="pending" header="Pending">
            <template #body="{ data }">
              <Badge :value="data.pending" severity="warning" />
            </template>
          </Column>
          <Column field="processing" header="Processing">
            <template #body="{ data }">
              <Badge :value="data.processing" severity="info" />
            </template>
          </Column>
          <Column field="throughput" header="Rate">
            <template #body="{ data }">
              <span class="rate-value">{{ data.throughput }} msg/min</span>
            </template>
          </Column>
          <Column>
            <template #body="{ data }">
              <Button 
                icon="pi pi-eye" 
                class="p-button-text p-button-sm"
                @click="$router.push(`/queues/${data.name}`)"
                v-tooltip="'View Details'"
              />
            </template>
          </Column>
        </DataTable>
        </template>
      </Card>

      <Card class="activity-feed-card">
        <template #title>
          <div class="card-header-custom">
            <span>Live Activity</span>
            <div class="live-indicator">
              <span class="pulse-dot"></span>
              <span>Live</span>
            </div>
          </div>
        </template>
        <template #content>
          <ActivityFeed :events="activityEvents" />
        </template>
      </Card>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue'
import { useToast } from 'primevue/usetoast'
import Card from 'primevue/card'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Badge from 'primevue/badge'
import Tag from 'primevue/tag'

import MetricCard from '../components/cards/MetricCard.vue'
import ThroughputChart from '../components/charts/ThroughputChart.vue'
import QueueDepthChart from '../components/charts/QueueDepthChart.vue'
import ActivityFeed from '../components/common/ActivityFeed.vue'

import api from '../services/api.js'
import websocket from '../services/websocket.js'
import { formatNumber } from '../utils/helpers.js'

const toast = useToast()
const loading = ref(false)

// Metrics data
const metrics = ref({
  totalMessages: 0,
  pending: 0,
  processing: 0,
  completedToday: 0,
  failedToday: 0,
  messagesPerSecond: '0',
  messageTrend: 'up'
})

// Chart data - Initialize with empty structure
const throughputData = ref({
  labels: [],
  datasets: [
    {
      label: 'Incoming',
      data: [],
      borderColor: '#3b82f6',
      backgroundColor: 'rgba(59, 130, 246, 0.1)',
      tension: 0.4
    },
    {
      label: 'Completed',
      data: [],
      borderColor: '#10b981',
      backgroundColor: 'rgba(16, 185, 129, 0.1)',
      tension: 0.4
    },
    {
      label: 'Failed',
      data: [],
      borderColor: '#ef4444',
      backgroundColor: 'rgba(239, 68, 68, 0.1)',
      tension: 0.4
    }
  ]
})
const queueDepthData = ref({
  labels: [],
  datasets: [
    {
      label: 'Pending',
      data: [],
      backgroundColor: '#f59e0b'
    },
    {
      label: 'Processing',
      data: [],
      backgroundColor: '#3b82f6'
    }
  ]
})
const currentThroughput = ref(0)
const activeQueues = ref(0)

// Table data
const topQueues = ref([])

// Activity feed
const activityEvents = ref([])
const maxActivityEvents = 20

// Fetch dashboard data
const fetchData = async () => {
  try {
    loading.value = true
    
    // Fetch all data in parallel
    const [overview, throughput, depths, queues] = await Promise.all([
      api.getSystemOverview(),
      api.getThroughput(),
      api.getQueueDepths(),
      api.getQueues()
    ])

    // Update metrics
    if (overview) {
      metrics.value = {
        totalMessages: overview.messages?.total || 0,
        pending: overview.messages?.pending || 0,
        processing: overview.messages?.processing || 0,
        completedToday: overview.messages?.completed || 0,
        failedToday: overview.messages?.failed || 0,
        messagesPerSecond: calculateMessagesPerSecond(throughput),
        messageTrend: calculateTrend(overview.messages?.total)
      }
    }

    // Process throughput data for chart
    if (throughput?.throughput) {
      processThroughputData(throughput.throughput)
    }

    // Process queue depths for chart
    if (depths?.depths) {
      processQueueDepthData(depths.depths)
    }

    // Process top queues
    if (queues?.queues) {
      processTopQueues(queues.queues)
    }

  } catch (error) {
    console.error('Failed to fetch dashboard data:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load dashboard data',
      life: 3000
    })
  } finally {
    loading.value = false
  }
}

// Process throughput data for chart
const processThroughputData = (data) => {
  const labels = data.slice(-30).map(d => {
    const date = new Date(d.timestamp)
    return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
  })

  const incoming = data.slice(-30).map(d => d.incoming?.messagesPerMinute || 0)
  const completed = data.slice(-30).map(d => d.completed?.messagesPerMinute || 0)
  const failed = data.slice(-30).map(d => d.failed?.messagesPerMinute || 0)

  throughputData.value = {
    labels,
    datasets: [
      {
        label: 'Incoming',
        data: incoming,
        borderColor: '#3b82f6',
        backgroundColor: 'rgba(59, 130, 246, 0.1)',
        tension: 0.4
      },
      {
        label: 'Completed',
        data: completed,
        borderColor: '#10b981',
        backgroundColor: 'rgba(16, 185, 129, 0.1)',
        tension: 0.4
      },
      {
        label: 'Failed',
        data: failed,
        borderColor: '#ef4444',
        backgroundColor: 'rgba(239, 68, 68, 0.1)',
        tension: 0.4
      }
    ]
  }

  // Current throughput
  const latest = data[data.length - 1]
  currentThroughput.value = latest?.incoming?.messagesPerMinute || 0
}

// Process queue depth data for chart
const processQueueDepthData = (depths) => {
  // Sort by depth and take top 10
  const sorted = depths
    .sort((a, b) => b.depth - a.depth)
    .slice(0, 10)

  const labels = sorted.map(q => q.queue)
  const pending = sorted.map(q => q.depth || 0)
  const processing = sorted.map(q => q.processing || 0)

  queueDepthData.value = {
    labels,
    datasets: [
      {
        label: 'Pending',
        data: pending,
        backgroundColor: '#f59e0b'
      },
      {
        label: 'Processing',
        data: processing,
        backgroundColor: '#3b82f6'
      }
    ]
  }

  activeQueues.value = depths.filter(q => q.depth > 0 || q.processing > 0).length
}

// Process top queues for table
const processTopQueues = (queues) => {
  topQueues.value = queues
    .map(q => ({
      name: q.name,
      namespace: q.namespace,
      pending: q.messages?.pending || 0,
      processing: q.messages?.processing || 0,
      throughput: Math.round(Math.random() * 100) // TODO: Calculate real throughput
    }))
    .sort((a, b) => (b.pending + b.processing) - (a.pending + a.processing))
    .slice(0, 5)
}

// Calculate messages per second
const calculateMessagesPerSecond = (throughput) => {
  if (!throughput?.throughput || throughput.throughput.length === 0) return '0'
  const latest = throughput.throughput[throughput.throughput.length - 1]
  return (latest?.incoming?.messagesPerSecond || 0).toFixed(1)
}

// Calculate trend
const calculateTrend = (current) => {
  // TODO: Compare with previous value
  return 'up'
}

// Handle WebSocket events
const handleWebSocketEvent = (event, data) => {
  // Add to activity feed
  const eventItem = {
    id: Date.now(),
    event,
    data,
    timestamp: new Date()
  }
  
  activityEvents.value.unshift(eventItem)
  
  // Keep only last N events
  if (activityEvents.value.length > maxActivityEvents) {
    activityEvents.value = activityEvents.value.slice(0, maxActivityEvents)
  }

  // Update metrics based on event
  if (event === 'message.pushed') {
    metrics.value.pending++
  } else if (event === 'message.processing') {
    metrics.value.pending--
    metrics.value.processing++
  } else if (event === 'message.completed') {
    metrics.value.processing--
    metrics.value.completedToday++
  } else if (event === 'message.failed') {
    metrics.value.processing--
    metrics.value.failedToday++
  }
}

// Refresh data
const refreshData = () => {
  fetchData()
  toast.add({
    severity: 'info',
    summary: 'Refreshing',
    detail: 'Dashboard data refreshed',
    life: 2000
  })
}

// Setup WebSocket listeners
const setupWebSocketListeners = () => {
  websocket.on('message.pushed', (data) => handleWebSocketEvent('message.pushed', data))
  websocket.on('message.processing', (data) => handleWebSocketEvent('message.processing', data))
  websocket.on('message.completed', (data) => handleWebSocketEvent('message.completed', data))
  websocket.on('message.failed', (data) => handleWebSocketEvent('message.failed', data))
  websocket.on('queue.created', (data) => handleWebSocketEvent('queue.created', data))
  websocket.on('system.stats', (data) => {
    // Update system stats
    if (data.pending !== undefined) metrics.value.pending = data.pending
    if (data.processing !== undefined) metrics.value.processing = data.processing
  })
}

let refreshInterval = null

onMounted(() => {
  fetchData()
  setupWebSocketListeners()
  
  // Auto-refresh every 30 seconds
  refreshInterval = setInterval(() => {
    fetchData()
  }, 30000)
})

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})
</script>

<style scoped>
.dashboard {
  max-width: 1600px;
  margin: 0 auto;
}

.dashboard-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 2rem;
}

.dashboard-header h1 {
  font-size: 2rem;
  color: var(--gray-800);
  margin: 0;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 0.75rem;
  margin-bottom: 1.5rem;
}

.charts-row {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: 1.25rem;
  margin-bottom: 2rem;
}

.bottom-row {
  display: grid;
  grid-template-columns: 3fr 2fr;
  gap: 1.25rem;
}

.chart-card {
  min-height: 400px;
}

.chart-card :deep(.p-card-content) {
  height: 350px;
}

.queue-list-card {
  min-height: 400px;
}

.activity-feed-card {
  min-height: 400px;
  max-height: 600px;
  overflow: hidden;
}

.card-header-custom {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.live-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  color: var(--success-color);
  font-size: 0.875rem;
  font-weight: 500;
}

.pulse-dot {
  width: 8px;
  height: 8px;
  background: var(--success-color);
  border-radius: 50%;
  animation: pulse 2s infinite;
}

.rate-value {
  font-weight: 500;
  color: var(--primary-color);
}

.text-muted {
  color: var(--gray-400);
}

/* Responsive Design */
@media (max-width: 1400px) {
  .bottom-row {
    grid-template-columns: 1fr 1fr;
  }
}

@media (max-width: 1200px) {
  .charts-row {
    grid-template-columns: 1fr;
  }
  
  .bottom-row {
    grid-template-columns: 1fr;
  }
  
  .chart-card {
    min-height: 350px;
  }
}

@media (max-width: 768px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
    gap: 0.5rem;
  }
  
  .charts-row,
  .bottom-row {
    gap: 0.75rem;
  }
  
  .dashboard-header {
    padding: 0;
    margin-bottom: 0.75rem;
  }
  
  .dashboard-header h1 {
    font-size: 1.25rem;
  }
  
  .chart-card {
    min-height: 250px;
  }
  
  .chart-card :deep(.p-card-content) {
    height: 200px;
    padding: 0.5rem;
  }
  
  .queue-list-card,
  .activity-feed-card {
    min-height: 250px;
  }
  
  /* Hide table columns on mobile */
  :deep(.p-datatable) .hide-mobile {
    display: none !important;
  }
}

@media (max-width: 480px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
    gap: 0.375rem;
  }
  
  .dashboard-header {
    flex-direction: column;
    align-items: stretch;
    gap: 0.5rem;
  }
  
  .dashboard-header h1 {
    font-size: 1.125rem;
  }
  
  .header-actions {
    width: 100%;
  }
  
  .header-actions :deep(.p-button) {
    width: 100%;
    font-size: 0.8125rem;
    padding: 0.375rem 0.75rem;
  }
  
  .chart-card {
    min-height: 200px;
  }
  
  .chart-card :deep(.p-card-content) {
    height: 150px;
  }
}
</style>
