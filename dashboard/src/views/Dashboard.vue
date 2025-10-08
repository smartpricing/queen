<template>
  <div class="dashboard">

    <!-- Metric Cards -->
    <div class="metrics-grid">
      <MetricCard 
        title="Total Messages"
        :value="metrics.totalMessages"
        icon="pi pi-envelope"
        color="primary"
        :loading="loading"
        :sparklineData="sparklineData.messages"
      />
      <MetricCard 
        title="Pending"
        :value="metrics.pending"
        icon="pi pi-clock"
        color="warning"
        :loading="loading"
        :sparklineData="sparklineData.pending"
      />
      <MetricCard 
        title="Processing"
        :value="metrics.processing"
        icon="pi pi-spin pi-spinner"
        color="info"
        :loading="loading"
        :sparklineData="sparklineData.processing"
      />
      <MetricCard 
        title="Completed Today"
        :value="metrics.completedToday"
        icon="pi pi-check-circle"
        color="success"
        :loading="loading"
        :sparklineData="sparklineData.completed"
      />
      <MetricCard 
        title="Failed Today"
        :value="metrics.failedToday"
        icon="pi pi-times-circle"
        color="danger"
        :loading="loading"
        :sparklineData="sparklineData.failed"
      />
    </div>

    <!-- Charts Row -->
    <div class="charts-row">
      <ThroughputChart 
        title="Message Throughput (Last Hour)"
        :data="throughputData" 
        :loading="loading"
        @refresh="refreshData"
      />

      <QueueDepthChart 
        title="Queue Depths"
        :data="queueDepthData" 
        :loading="loading"
        @refresh="refreshData"
      />
    </div>

    <!-- Queue List and Activity Feed -->
    <div class="bottom-row">
      <div class="card-v3">
        <div class="card-header">
          <h3>Top Queues by Activity</h3>
          <Button 
            label="View All" 
            class="btn-secondary"
            @click="$router.push('/queues')"
          />
        </div>
        
        <DataTable 
          :value="topQueues" 
          :loading="loading"
          responsiveLayout="scroll"
          :rows="5"
          class="dark-table-v3"
        >
          <Column field="name" header="Queue">
            <template #body="{ data }">
              <div class="queue-name-cell">
                <div class="queue-icon">Q</div>
                <span>{{ data.name }}</span>
              </div>
            </template>
          </Column>
          <Column field="namespace" header="Namespace">
            <template #body="{ data }">
              <Tag v-if="data.namespace" :value="data.namespace" class="namespace-tag" />
              <span v-else class="text-muted">-</span>
            </template>
          </Column>
          <Column field="pending" header="Pending">
            <template #body="{ data }">
              <span class="status-pending">{{ data.pending }}</span>
            </template>
          </Column>
          <Column field="processing" header="Processing">
            <template #body="{ data }">
              <span class="status-processing">{{ data.processing }}</span>
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
                class="p-button-text p-button-sm action-btn"
                @click="$router.push(`/queues/${data.name}`)"
                v-tooltip="'View Details'"
              />
            </template>
          </Column>
        </DataTable>
      </div>

      <div class="activity-feed">
        <div class="activity-header">
          <h3>Live Activity</h3>
          <div class="live-indicator">
            <span class="pulse-dot"></span>
            <span>Live</span>
          </div>
        </div>
        
        <ActivityFeed :events="activityEvents" />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue'
import { useToast } from 'primevue/usetoast'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Tag from 'primevue/tag'

import MetricCard from '../components/cards/MetricCard.vue'
import ThroughputChart from '../components/charts/ThroughputChart.vue'
import QueueDepthChart from '../components/charts/QueueDepthChart.vue'
import ActivityFeed from '../components/common/ActivityFeed.vue'

import api from '../services/api.js'
import websocket from '../services/websocket.js'
import { formatNumber } from '../utils/helpers.js'

// Simple UUID generator for unique event IDs
const generateEventId = () => {
  return Date.now().toString(36) + Math.random().toString(36).substr(2, 9)
}

const toast = useToast()
const loading = ref(false)

// Metrics data
const metrics = ref({
  totalMessages: 0,
  pending: 0,
  processing: 0,
  completedToday: 0,
  failedToday: 0
})

// Sparkline data for metric cards
const sparklineData = ref({
  messages: [],
  pending: [],
  processing: [],
  completed: [],
  failed: []
})

// Chart data - Initialize with empty structure
const throughputData = ref({
  labels: [],
  datasets: [
    { label: 'Incoming', data: [] },
    { label: 'Completed', data: [] },
    { label: 'Failed', data: [] }
  ]
})

const queueDepthData = ref({
  labels: [],
  datasets: [
    { label: 'Pending', data: [] },
    { label: 'Processing', data: [] }
  ]
})

// Queue distribution data
const queueDistribution = ref([])

// Table data
const topQueues = ref([])

// Activity feed
const activityEvents = ref([])
const maxActivityEvents = 20

// Generate random sparkline data (for demo)
const generateSparklineData = () => {
  const generateArray = (base, variance) => {
    return Array.from({ length: 10 }, () => 
      base + Math.floor(Math.random() * variance - variance / 2)
    )
  }
  
  sparklineData.value = {
    messages: generateArray(1000, 200),
    pending: generateArray(50, 20),
    processing: generateArray(30, 10),
    completed: generateArray(800, 150),
    failed: generateArray(10, 5)
  }
}

// Calculate queue distribution
const calculateQueueDistribution = (queues) => {
  // Use mock data if no real queues
  if (!queues || queues.length === 0) {
    queues = [
      { name: 'email-queue', pending: 120, processing: 30 },
      { name: 'notification-queue', pending: 85, processing: 20 },
      { name: 'analytics-queue', pending: 200, processing: 45 },
      { name: 'payment-queue', pending: 45, processing: 10 },
      { name: 'report-queue', pending: 30, processing: 5 }
    ]
  }
  
  const colors = ['#ec4899', '#10b981', '#f59e0b', '#3b82f6', '#8b5cf6', '#ef4444']
  const total = queues.reduce((sum, q) => sum + (q.pending || 0) + (q.processing || 0), 0)
  
  queueDistribution.value = queues
    .slice(0, 6)
    .map((queue, index) => {
      const count = (queue.pending || 0) + (queue.processing || 0)
      return {
        name: queue.name,
        count,
        percentage: total > 0 ? Math.round((count / total) * 100) : 0,
        color: colors[index % colors.length]
      }
    })
    .filter(item => item.percentage > 0)
}

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
        failedToday: overview.messages?.failed || 0
      }
    }

    // Process throughput data for chart - always process to show mock data if empty
    processThroughputData(throughput?.throughput || [])

    // Process queue depths for chart - always process to show mock data if empty
    processQueueDepthData(depths?.depths || [])

    // Process top queues and distribution - always process to show mock data if empty
    processTopQueues(queues?.queues || [])
    calculateQueueDistribution(queues?.queues || [])

    // Generate sparkline data
    generateSparklineData()

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
  // Generate mock data if no real data available
  if (!data || data.length === 0) {
    const now = new Date()
    const mockData = []
    for (let i = 11; i >= 0; i--) {
      const time = new Date(now.getTime() - i * 5 * 60 * 1000)
      mockData.push({
        timestamp: time.toISOString(),
        incoming: { messagesPerMinute: Math.floor(Math.random() * 100) + 50 },
        completed: { messagesPerMinute: Math.floor(Math.random() * 80) + 40 },
        failed: { messagesPerMinute: Math.floor(Math.random() * 10) }
      })
    }
    data = mockData
  }

  // Take last 12 data points for hourly view and reverse to show chronological order (oldest to newest)
  const recentData = data.reverse().slice(-100)
  
  throughputData.value = {
    labels: recentData.map(item => {
      const date = new Date(item.timestamp)
      return date.toLocaleTimeString('en-US', { 
        hour: 'numeric', 
        minute: '2-digit' 
      })
    }),
    datasets: [
      {
        label: 'Incoming Messages',
        data: recentData.map(item => item.incoming?.messagesPerMinute || 0)
      },
      {
        label: 'Completed Messages',
        data: recentData.map(item => item.completed?.messagesPerMinute || 0)
      },
      {
        label: 'Failed Messages',
        data: recentData.map(item => item.failed?.messagesPerMinute || 0)
      }
    ]
  }
}

// Process queue depth data for chart
const processQueueDepthData = (data) => {
  // Generate mock data if no real data available
  if (!data || data.length === 0) {
    data = [
      { queue: 'email-queue', depth: 45, processing: 12 },
      { queue: 'notification-queue', depth: 32, processing: 8 },
      { queue: 'analytics-queue', depth: 28, processing: 5 },
      { queue: 'payment-queue', depth: 15, processing: 3 },
      { queue: 'report-queue', depth: 10, processing: 2 }
    ]
  }

  // Filter out queues with no messages and sort by total messages, take top 8
  const filtered = data.filter(item => (item.depth || 0) + (item.processing || 0) > 0)
  const sorted = [...filtered]
    .sort((a, b) => (b.depth + b.processing) - (a.depth + a.processing))
    .slice(0, 8)

  queueDepthData.value = {
    labels: sorted.map(item => item.queue),
    datasets: [
      {
        label: 'Pending',
        data: sorted.map(item => item.depth || 0)
      },
      {
        label: 'Processing',
        data: sorted.map(item => item.processing || 0)
      }
    ]
  }
}

// Process top queues for table
const processTopQueues = (queues) => {
  // Use mock data if no real queues
  if (!queues || queues.length === 0) {
    queues = [
      { name: 'email-queue', namespace: 'default', stats: { pending: 120, processing: 30, completed: 1500 } },
      { name: 'notification-queue', namespace: 'alerts', stats: { pending: 85, processing: 20, completed: 950 } },
      { name: 'analytics-queue', namespace: null, stats: { pending: 200, processing: 45, completed: 3200 } },
      { name: 'payment-queue', namespace: 'finance', stats: { pending: 45, processing: 10, completed: 420 } },
      { name: 'report-queue', namespace: null, stats: { pending: 30, processing: 5, completed: 280 } }
    ]
  }

  topQueues.value = queues
    .map(queue => ({
      name: queue.name,
      namespace: queue.namespace,
      pending: queue.stats?.pending || 0,
      processing: queue.stats?.processing || 0,
      throughput: Math.floor(Math.random() * 100) // Mock throughput
    }))
    .sort((a, b) => (b.pending + b.processing) - (a.pending + a.processing))
    .slice(0, 5)
}

// Handle WebSocket events
const handleWebSocketEvent = (wsMessage) => {
  // WebSocket message format: { event: "message.pushed", data: {...}, timestamp: "..." }
  // ActivityFeed expects: { event: "message.pushed", data: {...}, timestamp: Date }
  
  // Add to activity feed in the format ActivityFeed expects
  activityEvents.value.unshift({
    id: generateEventId(), // Use unique ID generator to avoid duplicate keys
    event: wsMessage.event || 'unknown',
    data: wsMessage.data || {},
    timestamp: wsMessage.timestamp ? new Date(wsMessage.timestamp) : new Date()
  })

  // Limit activity events
  if (activityEvents.value.length > maxActivityEvents) {
    activityEvents.value = activityEvents.value.slice(0, maxActivityEvents)
  }

  // Update metrics based on event type
  const eventType = wsMessage.event
  if (eventType === 'message.pushed') {
    metrics.value.pending++
    metrics.value.totalMessages++
  } else if (eventType === 'message.popped') {
    metrics.value.pending = Math.max(0, metrics.value.pending - 1)
    metrics.value.processing++
  } else if (eventType === 'message.completed' || eventType === 'message.acknowledged') {
    metrics.value.processing = Math.max(0, metrics.value.processing - 1)
    metrics.value.completedToday++
  } else if (eventType === 'message.failed') {
    metrics.value.processing = Math.max(0, metrics.value.processing - 1)
    metrics.value.failedToday++
  }
}

// Refresh data
const refreshData = () => {
  fetchData()
}

// Lifecycle
onMounted(() => {
  fetchData()
  
  // Subscribe to WebSocket events
  websocket.on('message', handleWebSocketEvent)
  
  // Auto-refresh every 30 seconds
  const refreshInterval = setInterval(() => {
    fetchData()
  }, 30000)
  
  // Store interval ID for cleanup
  window.dashboardRefreshInterval = refreshInterval
})

onUnmounted(() => {
  // Clean up WebSocket listener
  websocket.off('message', handleWebSocketEvent)
  
  // Clear refresh interval
  if (window.dashboardRefreshInterval) {
    clearInterval(window.dashboardRefreshInterval)
  }
})
</script>

<style scoped>
.dashboard {
  padding: 0;
  width: 100%;
  max-width: 1600px;
  margin: 0 auto;
}

/* Header removed - no longer needed */

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 1rem;
  margin-bottom: 1.5rem;
}

.charts-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1.5rem;
  margin-bottom: 1.5rem;
}

.bottom-row {
  display: grid;
  grid-template-columns: 1.5fr 1fr;
  gap: 1.5rem;
}

.card-v3 {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 1.5rem;
  position: relative;
  overflow: hidden;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.card-header h3 {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0;
}

/* Queue table styling */
.queue-name-cell {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.queue-icon {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: var(--gradient-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  font-weight: 600;
  font-size: 0.875rem;
}

.namespace-tag {
  background: rgba(236, 72, 153, 0.15);
  color: var(--primary-500);
  border: 1px solid rgba(236, 72, 153, 0.3);
}

.rate-value {
  color: var(--primary-500);
  font-weight: 600;
  font-size: 0.875rem;
}

.action-btn {
  color: var(--surface-500) !important;
}

.action-btn:hover {
  background: rgba(236, 72, 153, 0.1) !important;
  color: var(--primary-500) !important;
}

/* Activity feed styling */
.activity-feed {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 1.5rem;
  max-height: 500px;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.activity-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.activity-header h3 {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0;
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
  border-radius: 50%;
  background: var(--success-color);
  animation: pulse 2s infinite;
}

/* Distribution card is already styled in main.css */

/* Override DataTable styles for dark theme */
:deep(.dark-table-v3) {
  background: transparent !important;
  border: none !important;
}

:deep(.dark-table-v3 .p-datatable-thead > tr > th) {
  background: var(--surface-0) !important;
  color: var(--surface-400) !important;
  border-color: rgba(255, 255, 255, 0.05) !important;
}

:deep(.dark-table-v3 .p-datatable-tbody > tr) {
  background: transparent !important;
}

:deep(.dark-table-v3 .p-datatable-tbody > tr:hover) {
  background: rgba(236, 72, 153, 0.05) !important;
}

:deep(.dark-table-v3 .p-datatable-tbody > tr > td) {
  color: var(--surface-600) !important;
  border-color: rgba(255, 255, 255, 0.03) !important;
}

/* Responsive */
@media (max-width: 1024px) {
  .charts-row {
    grid-template-columns: 1fr;
  }
  
  .bottom-row {
    grid-template-columns: 1fr;
  }
  
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 768px) {
  .dashboard-header {
    flex-direction: column;
    align-items: flex-start;
    gap: 1rem;
  }
  
  .metrics-grid {
    grid-template-columns: 1fr;
  }
  
  .activity-feed {
    max-height: 400px;
  }
}
</style>