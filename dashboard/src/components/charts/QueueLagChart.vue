<template>
  <div class="queue-lag-chart">
    <div class="chart-header">
      <h3 class="chart-title">Queue Lag Analysis</h3>
      <div class="chart-controls">
        <button @click="refreshData" class="refresh-btn" :disabled="loading">
          <i class="pi pi-refresh" :class="{ 'pi-spin': loading }"></i>
        </button>
      </div>
    </div>

    <div v-if="loading" class="loading-state">
      <i class="pi pi-spinner pi-spin"></i>
      <span>Loading lag data...</span>
    </div>

    <div v-else-if="error" class="error-state">
      <i class="pi pi-exclamation-triangle"></i>
      <span>{{ error }}</span>
    </div>

    <div v-else-if="!lagData || lagData.length === 0" class="empty-state">
      <i class="pi pi-info-circle"></i>
      <span>No lag data available. Queues need completed messages to calculate lag.</span>
    </div>

    <div v-else class="lag-content">
      <!-- Lag Bar Chart -->
      <div class="lag-chart-container">
        <div class="chart-section">
          <h4 class="section-title">{{ getMetricLabel() }} by Queue</h4>
          <div class="bar-chart">
            <div v-for="queue in lagData" :key="queue.queue" class="bar-item">
              <div class="bar-label">{{ queue.queue }}</div>
              <div class="bar-container">
                <div 
                  class="bar-fill" 
                  :class="getLagSeverity(queue)"
                  :style="{ width: getBarWidth(queue) + '%' }"
                ></div>
                <span class="bar-value">{{ getMetricValue(queue) }}</span>
              </div>
              <div class="bar-details">
                <span class="backlog-count">{{ queue.totals.totalBacklog }} backlog</span>
                <span class="processing-time">{{ queue.totals.avgProcessingTime }} avg</span>
              </div>
            </div>
          </div>
        </div>

        <!-- Processing Time Chart -->
        <div class="chart-section">
          <h4 class="section-title">Average Processing Time</h4>
          <div class="bar-chart">
            <div v-for="queue in lagData" :key="`processing-${queue.queue}`" class="bar-item">
              <div class="bar-label">{{ queue.queue }}</div>
              <div class="bar-container">
                <div 
                  class="bar-fill processing-time-bar" 
                  :style="{ width: getProcessingTimeBarWidth(queue) + '%' }"
                ></div>
                <span class="bar-value">{{ queue.totals.avgProcessingTime }}</span>
              </div>
              <div class="bar-details">
                <span class="completed-count">{{ queue.totals.completedMessages }} completed (24h)</span>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Detailed Table -->
      <div class="lag-table-container">
        <table class="lag-table dark-table">
          <thead>
            <tr>
              <th>Queue</th>
              <th>Partition</th>
              <th>Pending</th>
              <th>Processing</th>
              <th>Total Backlog</th>
              <th>Avg Processing Time</th>
              <th>{{ getMetricLabel() }}</th>
              <th>Completed (24h)</th>
            </tr>
          </thead>
          <tbody>
            <template v-for="queue in lagData" :key="queue.queue">
              <tr v-for="partition in queue.partitions" :key="`${queue.queue}-${partition.name}`">
                <td>{{ queue.queue }}</td>
                <td>{{ partition.name }}</td>
                <td>{{ partition.stats.pendingCount }}</td>
                <td>{{ partition.stats.processingCount }}</td>
                <td>{{ partition.stats.totalBacklog }}</td>
                <td>{{ partition.stats.avgProcessingTime }}</td>
                <td :class="getPartitionLagSeverity(partition)">
                  {{ getPartitionMetricValue(partition) }}
                </td>
                <td>{{ partition.stats.completedMessages }}</td>
              </tr>
            </template>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue'
import api from '../../services/api.js'

// Reactive data
const lagData = ref([])
const loading = ref(false)
const error = ref(null)

// Methods
const refreshData = async () => {
  loading.value = true
  error.value = null
  
  try {
    const response = await api.getQueueLag()
    lagData.value = response.queues || []
  } catch (err) {
    console.error('Failed to fetch queue lag data:', err)
    error.value = err.message || 'Failed to load lag data'
  } finally {
    loading.value = false
  }
}

const getMetricLabel = () => {
  return 'Estimated Lag'
}

const getMetricValue = (queue) => {
  return queue.totals.estimatedLag
}

const getPartitionMetricValue = (partition) => {
  return partition.stats.estimatedLag
}

const getLagSeverity = (queue) => {
  const seconds = queue.totals.estimatedLagSeconds
    
  if (seconds === 0) return 'lag-none'
  if (seconds < 60) return 'lag-low'
  if (seconds < 300) return 'lag-medium'
  if (seconds < 1800) return 'lag-high'
  return 'lag-critical'
}

const getPartitionLagSeverity = (partition) => {
  const seconds = partition.stats.estimatedLagSeconds
    
  if (seconds === 0) return 'lag-none'
  if (seconds < 60) return 'lag-low'
  if (seconds < 300) return 'lag-medium'
  if (seconds < 1800) return 'lag-high'
  return 'lag-critical'
}

const getBarWidth = (queue) => {
  if (!lagData.value || lagData.value.length === 0) return 0
  
  const maxLagSeconds = Math.max(...lagData.value.map(q => q.totals.estimatedLagSeconds))
  
  if (maxLagSeconds === 0) return 0
  
  const currentLagSeconds = queue.totals.estimatedLagSeconds
    
  return Math.max((currentLagSeconds / maxLagSeconds) * 100, 2) // Minimum 2% for visibility
}

const getProcessingTimeBarWidth = (queue) => {
  if (!lagData.value || lagData.value.length === 0) return 0
  
  const maxProcessingTime = Math.max(...lagData.value.map(q => q.totals.avgProcessingTimeSeconds))
  
  if (maxProcessingTime === 0) return 0
  
  return Math.max((queue.totals.avgProcessingTimeSeconds / maxProcessingTime) * 100, 2)
}

// Lifecycle
onMounted(() => {
  refreshData()
})
</script>

<style scoped>
.queue-lag-chart {
  background: transparent;
  border-radius: 8px;
  padding: 1.5rem;
  position: relative;
  z-index: 2;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.chart-title {
  margin: 0;
  color: #ffffff;
  font-size: 1.25rem;
  font-weight: 600;
}

.chart-controls {
  display: flex;
  gap: 0.75rem;
  align-items: center;
}


.refresh-btn {
  padding: 0.5rem;
  border: 1px solid rgba(255, 255, 255, 0.3);
  border-radius: 4px;
  background: rgba(255, 255, 255, 0.1);
  color: #ffffff;
  cursor: pointer;
  transition: all 0.2s;
}

.refresh-btn:hover:not(:disabled) {
  background: rgba(236, 72, 153, 0.1);
  border-color: rgba(236, 72, 153, 0.3);
}

.refresh-btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.loading-state,
.error-state,
.empty-state {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
  padding: 2rem;
  color: #a1a1aa;
  font-style: italic;
}

.error-state {
  color: #ef4444;
}

.lag-content {
  padding: 0;
}

.lag-chart-container {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 2rem;
  margin-bottom: 2rem;
}

.chart-section {
  background: #0a0a0a;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
  padding: 1.5rem;
}

.section-title {
  margin: 0 0 1rem 0;
  color: #e4e4e7;
  font-size: 1rem;
  font-weight: 500;
}

.bar-chart {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.bar-item {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.bar-label {
  font-size: 0.875rem;
  color: #d4d4d8;
  font-weight: 500;
}

.bar-container {
  position: relative;
  height: 24px;
  background: #2a2a2a;
  border-radius: 4px;
  display: flex;
  align-items: center;
  overflow: hidden;
}

.bar-fill {
  height: 100%;
  border-radius: 4px;
  transition: width 0.3s ease;
  position: relative;
}

.processing-time-bar {
  background: linear-gradient(90deg, rgba(59, 130, 246, 0.6), rgba(59, 130, 246, 0.8));
}

.bar-value {
  position: absolute;
  right: 8px;
  font-size: 0.75rem;
  color: #ffffff;
  font-weight: 600;
  z-index: 2;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.8);
}

.bar-details {
  display: flex;
  gap: 1rem;
  font-size: 0.75rem;
  color: #a1a1aa;
}

.lag-table-container {
  overflow-x: auto;
  background: #0a0a0a;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
}

.dark-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.875rem;
}

.dark-table th,
.dark-table td {
  padding: 0.75rem;
  text-align: left;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.dark-table th {
  background: #0a0a0a;
  font-weight: 600;
  color: #d4d4d8;
  font-size: 0.8rem;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.dark-table td {
  color: #e4e4e7;
}

.dark-table tbody tr:hover {
  background: rgba(236, 72, 153, 0.05);
}

/* Lag severity colors */
.lag-none {
  color: #10b981;
  background: linear-gradient(90deg, rgba(16, 185, 129, 0.2), rgba(16, 185, 129, 0.4));
}

.lag-low {
  color: #06b6d4;
  background: linear-gradient(90deg, rgba(6, 182, 212, 0.2), rgba(6, 182, 212, 0.4));
}

.lag-medium {
  color: #f59e0b;
  background: linear-gradient(90deg, rgba(245, 158, 11, 0.2), rgba(245, 158, 11, 0.4));
}

.lag-high {
  color: #f97316;
  background: linear-gradient(90deg, rgba(249, 115, 22, 0.2), rgba(249, 115, 22, 0.4));
}

.lag-critical {
  color: #ef4444;
  background: linear-gradient(90deg, rgba(239, 68, 68, 0.2), rgba(239, 68, 68, 0.4));
  font-weight: 600;
}

@media (max-width: 1024px) {
  .lag-chart-container {
    grid-template-columns: 1fr;
    gap: 1.5rem;
  }
}

@media (max-width: 768px) {
  .chart-header {
    flex-direction: column;
    gap: 1rem;
    align-items: stretch;
  }
  
  .chart-controls {
    justify-content: center;
  }
  
  .chart-section {
    padding: 1rem;
  }
  
  .lag-table-container {
    font-size: 0.8rem;
  }
  
  .dark-table th,
  .dark-table td {
    padding: 0.5rem;
  }
}
</style>
