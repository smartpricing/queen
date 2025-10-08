<template>
  <div class="analytics-view">
    <div class="analytics-grid">
      <div class="chart-container">
        <div class="chart-header">
          <h3 class="chart-title">System Throughput (Last Hour)</h3>
          <div class="chart-actions">
            <div class="realtime-indicator" v-tooltip="'Real-time updates via WebSocket'">
              <i class="pi pi-circle-fill realtime-dot"></i>
              <span class="realtime-text">Live</span>
            </div>
            <Button 
              icon="pi pi-refresh" 
              class="p-button-text p-button-sm"
              @click="fetchAnalytics"
              :loading="loading"
              v-tooltip="'Refresh'"
            />
          </div>
        </div>
        <ThroughputChart :data="throughputData" :loading="loading" />
      </div>

      <div class="card-v3">
        <h3 class="card-title">Queue Performance</h3>
        <DataTable :value="queueStats" :loading="loading" size="small" class="dark-table-v3">
          <Column field="queue" header="Queue" />
          <Column field="throughput" header="Throughput">
            <template #body="{ data }">
              <span class="metric-value">{{ data.throughput }} msg/min</span>
            </template>
          </Column>
          <Column field="successRate" header="Success Rate">
            <template #body="{ data }">
              <Tag :value="`${data.successRate}%`" :severity="getSuccessRateSeverity(data.successRate)" />
            </template>
          </Column>
        </DataTable>
      </div>

      <!-- Queue Lag Analysis -->
      <div class="lag-chart-wrapper full-width">
        <QueueLagChart />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { useToast } from 'primevue/usetoast'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Tag from 'primevue/tag'
import ThroughputChart from '../components/charts/ThroughputChart.vue'
import QueueLagChart from '../components/charts/QueueLagChart.vue'
import api from '../services/api.js'
import websocket from '../services/websocket.js'

const toast = useToast()
const loading = ref(false)

// Data
const throughputData = ref({
  labels: [],
  datasets: []
})
const queueStats = ref([])
const systemMetrics = ref({})
const topQueues = ref([])
const errorSummary = ref([])

// Real-time data tracking
const realtimeMetrics = ref({
  currentMinuteIncoming: 0,
  currentMinuteCompleted: 0,
  currentMinuteFailed: 0,
  lastMinuteTimestamp: null
})

// Queue depth cache for smooth updates
const queueDepthCache = ref(new Map())

// Smooth update functions
const updateThroughputDataPoint = (timestamp, incoming = 0, completed = 0, failed = 0) => {
  const currentData = throughputData.value
  if (!currentData.labels || currentData.labels.length === 0) return
  
  const timeLabel = new Date(timestamp).toLocaleTimeString('en-US', { 
    hour: 'numeric', 
    minute: '2-digit' 
  })
  
  // Add new data point and remove oldest if we have more than 60 points
  currentData.labels.push(timeLabel)
  currentData.datasets[0].data.push(incoming)
  currentData.datasets[1].data.push(completed)
  currentData.datasets[2].data.push(failed)
  
  // Keep only last 60 data points for smooth scrolling
  if (currentData.labels.length > 60) {
    currentData.labels.shift()
    currentData.datasets.forEach(dataset => dataset.data.shift())
  }
  
  // Trigger reactivity
  throughputData.value = { ...currentData }
}

const smoothUpdateQueueStats = (queueName, newStats) => {
  const currentStats = queueStats.value
  const existingIndex = currentStats.findIndex(q => q.queue === queueName)
  
  if (existingIndex >= 0) {
    // Smooth transition for existing queue
    const existing = currentStats[existingIndex]
    const updated = {
      ...existing,
      throughput: Math.round(newStats.completed / 60),
      successRate: newStats.total > 0 ? ((newStats.completed / newStats.total) * 100).toFixed(1) : existing.successRate
    }
    
    // Only update if values actually changed to avoid unnecessary re-renders
    if (updated.throughput !== existing.throughput || updated.successRate !== existing.successRate) {
      currentStats[existingIndex] = updated
      queueStats.value = [...currentStats]
    }
  } else if (newStats.total > 0) {
    // Add new queue
    currentStats.push({
      queue: queueName,
      throughput: Math.round(newStats.completed / 60),
      successRate: newStats.total > 0 ? ((newStats.completed / newStats.total) * 100).toFixed(1) : 100
    })
    queueStats.value = currentStats.sort((a, b) => b.throughput - a.throughput).slice(0, 10)
  }
}

const incrementRealtimeMetrics = (eventType) => {
  const now = new Date()
  const currentMinute = new Date(now.getFullYear(), now.getMonth(), now.getDate(), now.getHours(), now.getMinutes())
  
  // Reset counters if we've moved to a new minute
  if (!realtimeMetrics.value.lastMinuteTimestamp || 
      realtimeMetrics.value.lastMinuteTimestamp.getTime() !== currentMinute.getTime()) {
    
    // If we have data from the previous minute, add it to the chart
    if (realtimeMetrics.value.lastMinuteTimestamp) {
      updateThroughputDataPoint(
        realtimeMetrics.value.lastMinuteTimestamp,
        realtimeMetrics.value.currentMinuteIncoming,
        realtimeMetrics.value.currentMinuteCompleted,
        realtimeMetrics.value.currentMinuteFailed
      )
    }
    
    // Reset for new minute
    realtimeMetrics.value.currentMinuteIncoming = 0
    realtimeMetrics.value.currentMinuteCompleted = 0
    realtimeMetrics.value.currentMinuteFailed = 0
    realtimeMetrics.value.lastMinuteTimestamp = currentMinute
  }
  
  // Increment appropriate counter
  switch (eventType) {
    case 'message.pushed':
      realtimeMetrics.value.currentMinuteIncoming++
      break
    case 'message.completed':
      realtimeMetrics.value.currentMinuteCompleted++
      break
    case 'message.failed':
      realtimeMetrics.value.currentMinuteFailed++
      break
  }
}

// WebSocket event handlers
const handleQueueDepthUpdate = (data) => {
  // Update queue depth cache
  queueDepthCache.value.set(data.queue, data)
  
  // Calculate aggregated stats for this queue
  const totalStats = {
    pending: data.totalDepth || 0,
    processing: data.totalProcessing || 0,
    completed: 0,
    failed: 0,
    total: 0
  }
  
  // Add partition stats if available
  if (data.partitions) {
    Object.values(data.partitions).forEach(partition => {
      totalStats.completed += partition.completed || 0
      totalStats.failed += partition.failed || 0
      totalStats.total += (partition.depth || 0) + (partition.processing || 0) + (partition.completed || 0) + (partition.failed || 0)
    })
  }
  
  // Smooth update queue stats
  smoothUpdateQueueStats(data.queue, totalStats)
}

const handleSystemStats = (data) => {
  // Smooth update system metrics without full replacement
  const current = systemMetrics.value
  systemMetrics.value = {
    ...current,
    connections: data.connections || current.connections || 0,
    // Only update if significantly different to avoid jitter
    requestRate: Math.abs((data.recentCreated || 0) - (current.requestRate || 0)) > 5 
      ? data.recentCreated || 0 
      : current.requestRate || 0,
    latency: current.latency || 0, // Keep existing latency
    errorRate: current.errorRate || 0, // Keep existing error rate
    memory: current.memory || 0, // Keep existing memory
    uptime: current.uptime || 0 // Keep existing uptime
  }
}

const handleWebSocketMessage = (wsMessage) => {
  const { event, data } = wsMessage
  
  switch (event) {
    case 'queue.depth':
      handleQueueDepthUpdate(data)
      break
    case 'system.stats':
      handleSystemStats(data)
      break
    case 'message.pushed':
    case 'message.completed':
    case 'message.failed':
      incrementRealtimeMetrics(event)
      break
  }
}

// Fetch analytics data
const fetchAnalytics = async (silent = false) => {
  try {
    // Only show loading indicator for initial load, not for periodic refreshes
    if (!silent) {
      loading.value = true
    }
    
    // Fetch throughput data
    const throughput = await api.getThroughput()
    processThroughputData(throughput?.throughput || [])
    
    // Fetch real queue stats
    try {
      const queueStatsData = await api.getQueueStats()
      if (queueStatsData && Array.isArray(queueStatsData) && queueStatsData.length > 0) {
        // Process the array of queue-partition data and aggregate by queue
        const queueMap = new Map()
        
        queueStatsData.forEach(item => {
          const queueName = item.queue
          const stats = item.stats
          
          if (!queueMap.has(queueName)) {
            queueMap.set(queueName, {
              queue: queueName,
              pending: 0,
              processing: 0,
              completed: 0,
              failed: 0,
              total: 0
            })
          }
          
          const aggregated = queueMap.get(queueName)
          aggregated.pending += stats.pending || 0
          aggregated.processing += stats.processing || 0
          aggregated.completed += stats.completed || 0
          aggregated.failed += stats.failed || 0
          aggregated.total += stats.total || 0
        })
        
        // Convert to array and calculate throughput and success rate
        queueStats.value = Array.from(queueMap.values())
          .filter(queue => queue.total > 0) // Only show queues with activity
          .map(queue => ({
            queue: queue.queue,
            throughput: Math.round(queue.completed / 60), // Rough throughput estimate (completed per minute)
            successRate: queue.total > 0 ? ((queue.completed / queue.total) * 100).toFixed(1) : 0
          }))
          .sort((a, b) => b.throughput - a.throughput) // Sort by throughput descending
          .slice(0, 10) // Limit to top 10
      } else {
        // Fallback: try to get queue data and calculate stats
        const queues = await api.getQueues()
        if (queues?.queues && queues.queues.length > 0) {
          queueStats.value = queues.queues
            .filter(queue => queue.stats && (queue.stats.completed > 0 || queue.stats.pending > 0))
            .map(queue => ({
              queue: queue.name,
              throughput: Math.round((queue.stats.completed || 0) / 60), // Convert to per minute
              successRate: queue.stats.total > 0 ? ((queue.stats.completed / queue.stats.total) * 100).toFixed(1) : 100
            }))
            .slice(0, 10) // Limit to top 10
        } else {
          // Final fallback to mock data
          queueStats.value = [
            { queue: 'email-queue', throughput: 120, successRate: 98.5 },
            { queue: 'notification-queue', throughput: 85, successRate: 99.2 },
            { queue: 'analytics-queue', throughput: 200, successRate: 95.8 },
            { queue: 'payment-queue', throughput: 45, successRate: 99.9 }
          ]
        }
      }
    } catch (error) {
      console.error('Failed to fetch queue stats:', error)
      // Use mock data as fallback
      queueStats.value = [
        { queue: 'email-queue', throughput: 120, successRate: 98.5 },
        { queue: 'notification-queue', throughput: 85, successRate: 99.2 },
        { queue: 'analytics-queue', throughput: 200, successRate: 95.8 },
        { queue: 'payment-queue', throughput: 45, successRate: 99.9 }
      ]
    }
    
    // Fetch real system metrics from API
    try {
      const health = await api.getHealth()
      systemMetrics.value = {
        connections: health?.connections || 0,
        memory: health?.memory || 0,
        requestRate: health?.requestRate || 0,
        latency: health?.latency || 0,
        errorRate: health?.errorRate || 0,
        uptime: parseInt(health?.uptime) || 0
      }
    } catch (error) {
      // Use defaults if API fails
      systemMetrics.value = {
        connections: 0,
        memory: 0,
        requestRate: 0,
        latency: 0,
        errorRate: 0,
        uptime: 0
      }
    }
    
    // Mock top queues
    topQueues.value = [
      { name: 'analytics-queue', messagesProcessed: 15420 },
      { name: 'email-queue', messagesProcessed: 12350 },
      { name: 'notification-queue', messagesProcessed: 8920 }
    ]
    
    // Mock error summary
    errorSummary.value = [
      { queue: 'payment-queue', errors: 2, errorRate: 0.1 },
      { queue: 'email-queue', errors: 15, errorRate: 0.5 },
      { queue: 'analytics-queue', errors: 85, errorRate: 4.2 }
    ]
    
  } catch (error) {
    console.error('Failed to fetch analytics:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load analytics data',
      life: 3000
    })
  } finally {
    loading.value = false
  }
}

// Process throughput data for chart
const processThroughputData = (data) => {
  if (!data || data.length === 0) {
    // Only generate mock data if we don't have existing data
    if (!throughputData.value.labels || throughputData.value.labels.length === 0) {
      const now = new Date()
      const mockData = []
      for (let i = 23; i >= 0; i--) {
        const time = new Date(now.getTime() - i * 60 * 60 * 1000)
        mockData.push({
          timestamp: time.toISOString(),
          incoming: { messagesPerMinute: Math.floor(Math.random() * 200) + 100 },
          completed: { messagesPerMinute: Math.floor(Math.random() * 180) + 80 },
          failed: { messagesPerMinute: Math.floor(Math.random() * 20) }
        })
      }
      data = mockData
    } else {
      // Keep existing data if API returns empty
      return
    }
  }

  // Take last 60 data points for recent view and reverse to show chronological order (oldest to newest)
  const recentData = data.reverse().slice(-60)
  
  // Only update if we have significant changes to avoid unnecessary re-renders
  const newLabels = recentData.map(item => {
    const date = new Date(item.timestamp)
    return date.toLocaleTimeString('en-US', { 
      hour: 'numeric', 
      minute: '2-digit' 
    })
  })
  
  // Check if data has actually changed
  const currentLabels = throughputData.value.labels || []
  const hasChanged = newLabels.length !== currentLabels.length || 
    newLabels[newLabels.length - 1] !== currentLabels[currentLabels.length - 1]
  
  if (hasChanged) {
    throughputData.value = {
      labels: newLabels,
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
}

// Get severity for success rate
const getSuccessRateSeverity = (rate) => {
  if (rate >= 99) return 'success'
  if (rate >= 95) return 'warning'
  return 'danger'
}

// Format bytes
const formatBytes = (bytes) => {
  if (!bytes) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i]
}

// Format uptime
const formatUptime = (seconds) => {
  if (!seconds) return '-'
  const days = Math.floor(seconds / 86400)
  const hours = Math.floor((seconds % 86400) / 3600)
  if (days > 0) {
    return `${days}d ${hours}h`
  }
  return `${hours}h`
}

let refreshInterval = null

onMounted(() => {
  fetchAnalytics()
  
  // Subscribe to WebSocket events for real-time updates
  websocket.on('message', handleWebSocketMessage)
  
  // Reduced polling frequency - now every 5 minutes instead of 30 seconds
  // WebSocket handles real-time updates, polling is just for data consistency
  refreshInterval = setInterval(() => {
    fetchAnalytics(true) // Silent refresh to avoid loading indicators
  }, 300000) // 5 minutes
})

onUnmounted(() => {
  // Clean up WebSocket listeners
  websocket.off('message', handleWebSocketMessage)
  
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})
</script>

<style scoped>
.analytics-view {
  padding: 0;
}

.analytics-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
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

.card-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0 0 1.5rem 0;
}

.chart-container {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 1.5rem;
  height: 100%;
  display: flex;
  flex-direction: column;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.chart-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0;
}

.chart-actions {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.realtime-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.25rem 0.75rem;
  background: rgba(34, 197, 94, 0.1);
  border: 1px solid rgba(34, 197, 94, 0.2);
  border-radius: 12px;
  font-size: 0.75rem;
  font-weight: 500;
}

.realtime-dot {
  color: #22c55e;
  font-size: 0.5rem;
  animation: pulse 2s infinite;
}

.realtime-text {
  color: #22c55e;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.5;
  }
}

.system-metrics-card {
  margin-top: 1.5rem;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 1.5rem;
}

.metric-item {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.metric-label {
  font-size: 0.75rem;
  color: var(--surface-400);
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.metric-value {
  font-size: 1.25rem;
  font-weight: 600;
  color: var(--primary-500);
}

.queue-name-cell {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.queue-icon-small {
  width: 24px;
  height: 24px;
  border-radius: 6px;
  background: linear-gradient(135deg, #ec4899 0%, #db2777 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  font-weight: 600;
  font-size: 0.75rem;
}

/* DataTable dark theme overrides */
:deep(.dark-table-v3) {
  background: transparent !important;
  border: none !important;
}

:deep(.dark-table-v3 .p-datatable-thead > tr > th) {
  background: transparent !important;
  color: var(--surface-400) !important;
  border-color: rgba(255, 255, 255, 0.1) !important;
  padding: 0.75rem;
}

:deep(.dark-table-v3 .p-datatable-tbody > tr) {
  background: transparent !important;
}

:deep(.dark-table-v3 .p-datatable-tbody > tr:hover) {
  background: rgba(236, 72, 153, 0.05) !important;
}

:deep(.dark-table-v3 .p-datatable-tbody > tr > td) {
  color: var(--surface-600) !important;
  border-color: rgba(255, 255, 255, 0.05) !important;
  padding: 0.625rem 0.75rem;
}

.full-width {
  grid-column: 1 / -1;
}

.lag-chart-wrapper {
  background: #0a0a0a;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  overflow: hidden;
  position: relative;
  z-index: 1;
}

/* Responsive */
@media (max-width: 1024px) {
  .analytics-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 768px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>