<template>
  <div class="analytics-page">
    <div class="page-header">
      <h1 class="page-title">Analytics</h1>
      <div class="header-actions">
        <Dropdown 
          v-model="timeRange" 
          :options="timeRanges" 
          optionLabel="label"
          optionValue="value"
          class="w-10rem"
        />
        <Button 
          icon="pi pi-refresh" 
          label="Refresh"
          @click="loadAnalytics"
        />
      </div>
    </div>

    <!-- Key Metrics -->
    <div class="grid">
      <div class="col-12 md:col-6 lg:col-3">
        <Card class="metric-card">
          <template #content>
            <div class="metric">
              <div class="metric-label">Total Messages</div>
              <div class="metric-value">{{ formatNumber(metrics.totalMessages) }}</div>
              <div class="metric-change" :class="metrics.messagesChange >= 0 ? 'positive' : 'negative'">
                <i :class="metrics.messagesChange >= 0 ? 'pi pi-arrow-up' : 'pi pi-arrow-down'"></i>
                {{ Math.abs(metrics.messagesChange) }}% from last period
              </div>
            </div>
          </template>
        </Card>
      </div>
      <div class="col-12 md:col-6 lg:col-3">
        <Card class="metric-card">
          <template #content>
            <div class="metric">
              <div class="metric-label">Success Rate</div>
              <div class="metric-value">{{ metrics.successRate }}%</div>
              <ProgressBar :value="metrics.successRate" :showValue="false" />
            </div>
          </template>
        </Card>
      </div>
      <div class="col-12 md:col-6 lg:col-3">
        <Card class="metric-card">
          <template #content>
            <div class="metric">
              <div class="metric-label">Avg Processing Time</div>
              <div class="metric-value">{{ metrics.avgProcessingTime }}ms</div>
              <div class="metric-change" :class="metrics.processingChange <= 0 ? 'positive' : 'negative'">
                <i :class="metrics.processingChange <= 0 ? 'pi pi-arrow-down' : 'pi pi-arrow-up'"></i>
                {{ Math.abs(metrics.processingChange) }}% from last period
              </div>
            </div>
          </template>
        </Card>
      </div>
      <div class="col-12 md:col-6 lg:col-3">
        <Card class="metric-card">
          <template #content>
            <div class="metric">
              <div class="metric-label">Active Queues</div>
              <div class="metric-value">{{ metrics.activeQueues }}</div>
              <div class="metric-subtitle">{{ metrics.totalQueues }} total</div>
            </div>
          </template>
        </Card>
      </div>
    </div>

    <!-- Charts -->
    <div class="grid mt-3">
      <div class="col-12 lg:col-8">
        <Card>
          <template #title>Message Throughput</template>
          <template #content>
            <MultiMetricThroughputChart :data="throughputData" :showLag="true" />
          </template>
        </Card>
      </div>
      <div class="col-12 lg:col-4">
        <Card>
          <template #title>Status Distribution</template>
          <template #content>
            <Chart type="doughnut" :data="statusChartData" :options="statusChartOptions" />
          </template>
        </Card>
      </div>
    </div>

    <div class="grid mt-3">
      <div class="col-12 lg:col-6">
        <Card>
          <template #title>Queue Performance</template>
          <template #content>
            <DataTable :value="queuePerformance" responsiveLayout="scroll">
              <Column field="queue" header="Queue" :sortable="true"></Column>
              <Column field="messages" header="Messages" :sortable="true">
                <template #body="{ data }">
                  {{ formatNumber(data.messages) }}
                </template>
              </Column>
              <Column field="avgTime" header="Avg Time" :sortable="true">
                <template #body="{ data }">
                  {{ data.avgTime }}ms
                </template>
              </Column>
              <Column field="successRate" header="Success Rate" :sortable="true">
                <template #body="{ data }">
                  <div class="flex align-items-center gap-2">
                    <ProgressBar :value="data.successRate" :showValue="false" style="flex: 1" />
                    <span>{{ data.successRate }}%</span>
                  </div>
                </template>
              </Column>
            </DataTable>
          </template>
        </Card>
      </div>
      <div class="col-12 lg:col-6">
        <Card>
          <template #title>Error Analysis</template>
          <template #content>
            <DataTable :value="errorAnalysis" responsiveLayout="scroll">
              <Column field="error" header="Error Type" :sortable="true"></Column>
              <Column field="count" header="Count" :sortable="true">
                <template #body="{ data }">
                  {{ formatNumber(data.count) }}
                </template>
              </Column>
              <Column field="percentage" header="%" :sortable="true">
                <template #body="{ data }">
                  {{ data.percentage }}%
                </template>
              </Column>
              <Column field="trend" header="Trend">
                <template #body="{ data }">
                  <Tag 
                    :severity="getTrendSeverity(data.trend)" 
                    :value="data.trend > 0 ? `+${data.trend}%` : `${data.trend}%`"
                  />
                </template>
              </Column>
            </DataTable>
          </template>
        </Card>
      </div>
    </div>

    <!-- Heatmap -->
    <Card class="mt-3">
      <template #title>Activity Heatmap</template>
      <template #content>
        <div class="heatmap-container">
          <div class="heatmap-labels-y">
            <div v-for="hour in 24" :key="hour" class="heatmap-label-y">
              {{ String(hour - 1).padStart(2, '0') }}:00
            </div>
          </div>
          <div class="heatmap-grid">
            <div class="heatmap-labels-x">
              <div v-for="day in daysOfWeek" :key="day" class="heatmap-label-x">{{ day }}</div>
            </div>
            <div class="heatmap-cells">
              <div 
                v-for="(value, index) in heatmapData" 
                :key="index"
                class="heatmap-cell"
                :style="{ background: getHeatmapColor(value) }"
                v-tooltip="`${value} messages`"
              ></div>
            </div>
          </div>
          <div class="heatmap-legend">
            <span class="legend-label">Less</span>
            <div class="legend-gradient"></div>
            <span class="legend-label">More</span>
          </div>
        </div>
      </template>
    </Card>
  </div>
</template>

<script setup>
import { ref, onMounted, watch } from 'vue'
import { useToast } from 'primevue/usetoast'
import Button from 'primevue/button'
import Card from 'primevue/card'
import Dropdown from 'primevue/dropdown'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import ProgressBar from 'primevue/progressbar'
import Tag from 'primevue/tag'
import Chart from 'primevue/chart'
import MultiMetricThroughputChart from '../components/charts/MultiMetricThroughputChart.vue'
import { api } from '../utils/api'

const toast = useToast()

// State
const timeRange = ref('24h')
const metrics = ref({
  totalMessages: 0,
  messagesChange: 0,
  successRate: 0,
  avgProcessingTime: 0,
  processingChange: 0,
  activeWorkers: 0,
  totalWorkers: 0
})
const throughputData = ref([])
const statusChartData = ref({})
const statusChartOptions = ref({})
const queuePerformance = ref([])
const errorAnalysis = ref([])
const heatmapData = ref([])

const timeRanges = [
  { label: 'Last Hour', value: '1h' },
  { label: 'Last 24 Hours', value: '24h' },
  { label: 'Last 7 Days', value: '7d' },
  { label: 'Last 30 Days', value: '30d' }
]

const daysOfWeek = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']

// Methods
const loadAnalytics = async () => {
  try {
    // Load queues data once and share it to avoid multiple API calls
    const queuesResponse = await api.analytics.getQueues()
    const queues = queuesResponse.queues || []
    
    // Load all analytics data in parallel, sharing the queues data
    await Promise.all([
      loadMetricsWithData(queues),
      loadThroughput(),
      loadStatusDistributionWithData(queues),
      loadQueuePerformanceWithData(queues),
      loadErrorAnalysisWithData(queues),
      loadHeatmap()
    ])
    
    toast.add({
      severity: 'success',
      summary: 'Success',
      detail: 'Analytics data refreshed',
      life: 2000
    })
  } catch (error) {
    console.error('Failed to load analytics:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load analytics data',
      life: 3000
    })
  }
}

// Version that accepts pre-loaded queues data
const loadMetricsWithData = async (queues) => {
  try {
    
    // Calculate totals from real data
    let totalPending = 0
    let totalProcessing = 0
    let totalCompleted = 0
    let totalFailed = 0
    let totalDeadLetter = 0
    
    queues.forEach(q => {
      if (q.stats) {
        totalPending += q.stats.pending || 0
        totalProcessing += q.stats.processing || 0
        totalCompleted += q.stats.completed || 0
        totalFailed += q.stats.failed || 0
        totalDeadLetter += q.stats.deadLetter || 0
      }
    })
    
    const totalMessages = totalPending + totalProcessing + totalCompleted + totalFailed + totalDeadLetter
    const successRate = totalMessages > 0 ? ((totalCompleted / totalMessages) * 100).toFixed(1) : 0
    
    // Count active queues (those with pending or processing messages)
    const activeQueues = queues.filter(q => 
      q.stats && (q.stats.pending > 0 || q.stats.processing > 0)
    ).length
    
    metrics.value = {
      totalMessages,
      messagesChange: 0, // Would need historical data to calculate
      successRate: parseFloat(successRate),
      avgProcessingTime: 0, // Would need to calculate from actual processing times
      processingChange: 0,
      activeQueues: activeQueues,
      totalQueues: queues.length
    }
  } catch (error) {
    console.error('Failed to load metrics:', error)
    // Keep existing values on error
  }
}

const loadThroughput = async () => {
  try {
    const response = await api.analytics.getThroughput()
    console.log('Throughput API response:', response) // Debug log
    
    // The new API returns enhanced throughput data with multiple metrics
    if (response && response.throughput && response.throughput.length > 0) {
      throughputData.value = response.throughput
      console.log('Enhanced throughput data:', throughputData.value) // Debug log
    } else {
      throughputData.value = []
    }
  } catch (error) {
    console.error('Failed to load throughput:', error)
    throughputData.value = []
  }
}

// Version that accepts pre-loaded queues data
const loadStatusDistributionWithData = async (queues) => {
  try {
    
    // Calculate totals from real data
    let totalPending = 0
    let totalProcessing = 0
    let totalCompleted = 0
    let totalFailed = 0
    
    queues.forEach(q => {
      if (q.stats) {
        totalPending += q.stats.pending || 0
        totalProcessing += q.stats.processing || 0
        totalCompleted += q.stats.completed || 0
        totalFailed += q.stats.failed || 0
      }
    })
    
    statusChartData.value = {
      labels: ['Completed', 'Processing', 'Pending', 'Failed'],
      datasets: [{
        data: [totalCompleted, totalProcessing, totalPending, totalFailed],
        backgroundColor: ['#10b981', '#3b82f6', '#f59e0b', '#ef4444'],
        borderWidth: 0
      }]
    }
  } catch (error) {
    console.error('Failed to load status distribution:', error)
    // Set empty chart on error
    statusChartData.value = {
      labels: ['Completed', 'Processing', 'Pending', 'Failed'],
      datasets: [{
        data: [0, 0, 0, 0],
        backgroundColor: ['#10b981', '#3b82f6', '#f59e0b', '#ef4444'],
        borderWidth: 0
      }]
    }
  }
  
  statusChartOptions.value = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: {
        position: 'bottom',
        labels: {
          color: '#a3a3a3'
        }
      }
    }
  }
}

// Version that accepts pre-loaded queues data
const loadQueuePerformanceWithData = async (queues) => {
  try {
    
    // Convert queue stats to performance data
    queuePerformance.value = queues.map(q => ({
      queue: q.queue,
      messages: q.stats.total || 0,
      avgTime: 0, // Would need actual timing data
      successRate: q.stats.total > 0 
        ? Math.round((q.stats.completed / q.stats.total) * 100)
        : 0
    }))
  } catch (error) {
    console.error('Failed to load queue performance:', error)
    queuePerformance.value = []
  }
}

// Version that accepts pre-loaded queues data
const loadErrorAnalysisWithData = async (queues) => {
  try {
    // Would need to query actual error messages from the database
    // For now, show real failed message count
    
    let totalFailed = 0
    queues.forEach(q => {
      if (q.stats) {
        totalFailed += q.stats.failed || 0
      }
    })
    
    if (totalFailed > 0) {
      // We don't have error categorization yet, so show a single entry
      errorAnalysis.value = [
        { error: 'Failed Messages', count: totalFailed, percentage: 100, trend: 0 }
      ]
    } else {
      errorAnalysis.value = []
    }
  } catch (error) {
    console.error('Failed to load error analysis:', error)
    errorAnalysis.value = []
  }
}

const loadHeatmap = async () => {
  // Real heatmap would require hourly message counts from the database
  // For now, just set empty data to be honest about what we have
  console.log('Heatmap data not yet implemented - would need hourly message aggregation')
  
  // Create empty heatmap (7 days x 24 hours = 168 cells)
  heatmapData.value = new Array(168).fill(0)
  
  // TODO: Implement real query like:
  // SELECT DATE_TRUNC('hour', created_at) as hour, COUNT(*) 
  // FROM queen.messages 
  // WHERE created_at >= NOW() - INTERVAL '7 days'
  // GROUP BY hour
}

const formatNumber = (num) => {
  if (!num) return '0'
  return new Intl.NumberFormat().format(num)
}

const getTrendSeverity = (trend) => {
  if (trend > 0) return 'danger'
  if (trend < 0) return 'success'
  return 'secondary'
}

const getHeatmapColor = (value) => {
  const max = Math.max(...heatmapData.value)
  const intensity = value / max
  const hue = 270 - (intensity * 60) // Purple to yellow
  return `hsl(${hue}, 70%, ${30 + intensity * 40}%)`
}

// Backward compatibility wrapper functions for individual calls
const loadMetrics = () => api.analytics.getQueues().then(res => loadMetricsWithData(res.queues || []))
const loadStatusDistribution = () => api.analytics.getQueues().then(res => loadStatusDistributionWithData(res.queues || []))
const loadQueuePerformance = () => api.analytics.getQueues().then(res => loadQueuePerformanceWithData(res.queues || []))
const loadErrorAnalysis = () => api.analytics.getQueues().then(res => loadErrorAnalysisWithData(res.queues || []))

// Watch time range changes
watch(timeRange, () => {
  loadAnalytics()
})

// Lifecycle
onMounted(() => {
  loadAnalytics()
})
</script>

<style scoped>
.analytics-page {
  padding: 1rem;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.page-title {
  font-size: 1.75rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.header-actions {
  display: flex;
  gap: 0.5rem;
}

.metric-card {
  height: 100%;
  background: var(--bg-secondary) !important;
  border: 1px solid var(--border-subtle) !important;
}

/* Override PrimeVue Card styles for dark theme */
:deep(.p-card) {
  background: var(--bg-secondary) !important;
  border: 1px solid var(--border-subtle) !important;
  color: var(--text-primary) !important;
}

:deep(.p-card-title) {
  color: var(--text-primary) !important;
}

:deep(.p-card-content) {
  padding: 1rem !important;
}

:deep(.p-datatable) {
  background: transparent !important;
}

:deep(.p-datatable .p-datatable-header) {
  background: var(--bg-tertiary) !important;
  border: none !important;
}

:deep(.p-datatable .p-datatable-thead > tr > th) {
  background: var(--bg-tertiary) !important;
  color: var(--text-tertiary) !important;
  border-color: var(--border-subtle) !important;
}

:deep(.p-datatable .p-datatable-tbody > tr) {
  background: transparent !important;
  color: var(--text-secondary) !important;
}

:deep(.p-datatable .p-datatable-tbody > tr:hover) {
  background: var(--bg-tertiary) !important;
}

:deep(.p-datatable .p-datatable-tbody > tr > td) {
  border-color: var(--border-subtle) !important;
}

:deep(.p-progressbar) {
  background: var(--bg-tertiary) !important;
}

:deep(.p-progressbar-value) {
  background: var(--success) !important;
}

.metric {
  text-align: center;
}

.metric-label {
  font-size: 0.875rem;
  color: #a3a3a3;
  margin-bottom: 0.5rem;
}

.metric-value {
  font-size: 2rem;
  font-weight: 700;
  color: white;
  margin-bottom: 0.5rem;
}

.metric-change {
  font-size: 0.875rem;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.25rem;
}

.metric-change.positive {
  color: #10b981;
}

.metric-change.negative {
  color: #ef4444;
}

.metric-subtitle {
  font-size: 0.875rem;
  color: #a3a3a3;
}

.heatmap-container {
  display: flex;
  gap: 1rem;
  align-items: flex-start;
}

.heatmap-labels-y {
  display: flex;
  flex-direction: column;
  gap: 0;
}

.heatmap-label-y {
  height: 20px;
  font-size: 0.75rem;
  color: #a3a3a3;
  display: flex;
  align-items: center;
  justify-content: flex-end;
  padding-right: 0.5rem;
}

.heatmap-grid {
  flex: 1;
}

.heatmap-labels-x {
  display: grid;
  grid-template-columns: repeat(7, 1fr);
  gap: 2px;
  margin-bottom: 2px;
}

.heatmap-label-x {
  text-align: center;
  font-size: 0.75rem;
  color: #a3a3a3;
}

.heatmap-cells {
  display: grid;
  grid-template-columns: repeat(7, 1fr);
  grid-template-rows: repeat(24, 20px);
  gap: 2px;
}

.heatmap-cell {
  border-radius: 2px;
  cursor: pointer;
  transition: opacity 0.2s;
}

.heatmap-cell:hover {
  opacity: 0.8;
}

.heatmap-legend {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-top: 1rem;
  justify-content: center;
}

.legend-label {
  font-size: 0.75rem;
  color: #a3a3a3;
}

.legend-gradient {
  width: 100px;
  height: 10px;
  background: linear-gradient(to right, hsl(270, 70%, 30%), hsl(210, 70%, 70%));
  border-radius: 2px;
}

.w-10rem {
  width: 10rem;
}

.mt-3 {
  margin-top: 1rem;
}
</style>
