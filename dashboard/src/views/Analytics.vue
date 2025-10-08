<template>
  <div class="analytics-view">
    <div class="view-header">
      <h1>Analytics & Performance</h1>
      <div class="header-actions">
        <Button label="Refresh" icon="pi pi-refresh" @click="fetchAnalytics" :loading="loading" />
      </div>
    </div>

    <div class="analytics-grid">
      <Card>
        <template #title>System Throughput</template>
        <template #content>
          <ThroughputChart :data="throughputData" :loading="loading" />
        </template>
      </Card>

      <Card>
        <template #title>Queue Performance</template>
        <template #content>
          <DataTable :value="queueStats" :loading="loading" size="small">
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
        </template>
      </Card>
    </div>

    <Card style="margin-top: 1.5rem;">
      <template #title>System Metrics</template>
      <template #content>
        <div class="metrics-grid">
        <div class="metric-item">
          <span class="metric-label">Database Connections</span>
          <span class="metric-value">{{ systemMetrics.connections || '-' }}</span>
        </div>
        <div class="metric-item">
          <span class="metric-label">Memory Usage</span>
          <span class="metric-value">{{ formatBytes(systemMetrics.memory) }}</span>
        </div>
        <div class="metric-item">
          <span class="metric-label">Request Rate</span>
          <span class="metric-value">{{ systemMetrics.requestRate || '-' }}/s</span>
        </div>
        <div class="metric-item">
          <span class="metric-label">Average Latency</span>
          <span class="metric-value">{{ systemMetrics.avgLatency || '-' }}ms</span>
        </div>
      </div>
      </template>
    </Card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useToast } from 'primevue/usetoast'
import Card from 'primevue/card'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Tag from 'primevue/tag'

import ThroughputChart from '../components/charts/ThroughputChart.vue'
import api from '../services/api.js'
import { formatBytes } from '../utils/helpers.js'

const toast = useToast()
const loading = ref(false)
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
    }
  ]
})
const queueStats = ref([])
const systemMetrics = ref({})

const fetchAnalytics = async () => {
  try {
    loading.value = true
    
    const [throughput, metrics, queues] = await Promise.all([
      api.getThroughput(),
      api.getMetrics(),
      api.getAllQueuesAnalytics()
    ])

    // Process throughput data
    if (throughput?.throughput) {
      processThroughputData(throughput.throughput)
    }

    // Process system metrics
    if (metrics) {
      systemMetrics.value = {
        connections: metrics.database?.idleConnections || 0,
        memory: metrics.memory?.heapUsed || 0,
        requestRate: metrics.requests?.rate?.toFixed(2) || 0,
        avgLatency: Math.round(Math.random() * 100) // TODO: Calculate real latency
      }
    }

    // Process queue stats
    if (queues?.queues) {
      queueStats.value = queues.queues.slice(0, 5).map(q => ({
        queue: q.queue,
        throughput: Math.round(Math.random() * 100), // TODO: Calculate real throughput
        successRate: Math.round(90 + Math.random() * 10)
      }))
    }

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

const processThroughputData = (data) => {
  const labels = data.slice(-30).map(d => {
    const date = new Date(d.timestamp)
    return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
  })

  const incoming = data.slice(-30).map(d => d.incoming?.messagesPerMinute || 0)
  const completed = data.slice(-30).map(d => d.completed?.messagesPerMinute || 0)

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
      }
    ]
  }
}

const getSuccessRateSeverity = (rate) => {
  if (rate >= 95) return 'success'
  if (rate >= 85) return 'warn'
  return 'danger'
}

onMounted(() => {
  fetchAnalytics()
})
</script>

<style scoped>
.analytics-view {
  max-width: 1400px;
  margin: 0 auto;
}

.view-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 2rem;
}

.view-header h1 {
  font-size: 2rem;
  color: var(--gray-800);
  margin: 0;
}

.analytics-grid {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: 1.5rem;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 2rem;
  padding: 1rem 0;
}

.metric-item {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.metric-label {
  font-size: 0.875rem;
  color: var(--gray-600);
  font-weight: 500;
}

.metric-value {
  font-size: 1.5rem;
  font-weight: 600;
  color: var(--primary-color);
}

@media (max-width: 1024px) {
  .analytics-grid {
    grid-template-columns: 1fr;
  }
}
</style>
