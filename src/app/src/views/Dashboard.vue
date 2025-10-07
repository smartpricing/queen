<template>
  <div class="dashboard">
    <!-- Stats Cards -->
    <div class="stats-grid">
      <StatsCard
        title="Total Messages"
        :value="stats.total"
        icon="pi pi-inbox"
        color="purple"
        :loading="loading"
      />
      <StatsCard
        title="Pending"
        :value="stats.pending"
        icon="pi pi-clock"
        color="blue"
        :loading="loading"
      />
      <StatsCard
        title="Processing"
        :value="stats.processing"
        icon="pi pi-spin pi-spinner"
        color="yellow"
        :loading="loading"
      />
      <StatsCard
        title="Completed"
        :value="stats.completed"
        icon="pi pi-check-circle"
        color="green"
        :loading="loading"
      />
    </div>

    <!-- Charts Row -->
    <div class="charts-row">
      <Card class="chart-card">
        <template #title>
          <div class="card-header">
            <span>Throughput</span>
            <Tag severity="info">Last 5 min</Tag>
          </div>
        </template>
        <template #content>
          <MultiMetricThroughputChart :data="throughputData" :showLag="false" />
        </template>
      </Card>

      <Card class="chart-card">
        <template #title>
          <div class="card-header">
            <span>Queue Depths</span>
            <Button 
              icon="pi pi-refresh" 
              class="p-button-text p-button-sm"
              @click="refreshQueueDepths"
            />
          </div>
        </template>
        <template #content>
          <QueueDepthChart :data="queueDepthData" />
        </template>
      </Card>
    </div>

    <!-- Top Queues -->
    <Card class="queues-card">
      <template #title>
        <div class="card-header">
          <span>Top Queues</span>
          <div class="header-actions">
            <router-link to="/activity" class="view-all">
              <i class="pi pi-history"></i> Activity
            </router-link>
            <router-link to="/queues" class="view-all">
              View all <i class="pi pi-arrow-right"></i>
            </router-link>
          </div>
        </div>
      </template>
      <template #content>
        <DataTable 
          :value="topQueues" 
          :loading="loading"
          responsiveLayout="scroll"
          class="p-datatable-sm"
        >
          <Column field="queue" header="Queue">
            <template #body="{ data }">
              <router-link :to="`/queues/${encodeURIComponent(data.queue)}`" class="queue-link">
                {{ data.queue }}
              </router-link>
            </template>
          </Column>
          <Column field="priority" header="Priority">
            <template #body="{ data }">
              <Tag :severity="getPrioritySeverity(data.priority)">
                {{ data.priority }}
              </Tag>
            </template>
          </Column>
          <Column field="stats.pending" header="Pending">
            <template #body="{ data }">
              <Badge :value="data.stats?.pending || 0" severity="info" />
            </template>
          </Column>
          <Column field="stats.processing" header="Processing">
            <template #body="{ data }">
              <Badge :value="data.stats?.processing || 0" severity="warning" />
            </template>
          </Column>
          <Column field="stats.completed" header="Completed">
            <template #body="{ data }">
              <Badge :value="data.stats?.completed || 0" severity="success" />
            </template>
          </Column>
          <Column field="stats.failed" header="Failed">
            <template #body="{ data }">
              <Badge :value="data.stats?.failed || 0" severity="danger" />
            </template>
          </Column>
        </DataTable>
      </template>
    </Card>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import Card from 'primevue/card'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Tag from 'primevue/tag'
import Badge from 'primevue/badge'

import StatsCard from '../components/dashboard/StatsCard.vue'
import MultiMetricThroughputChart from '../components/charts/MultiMetricThroughputChart.vue'
import QueueDepthChart from '../components/charts/QueueDepthChart.vue'

import { useQueuesStore } from '../stores/queues'
import { api } from '../utils/api'

const queuesStore = useQueuesStore()

const loading = ref(false)
const throughputData = ref([])
const queueDepthData = ref([])
let refreshInterval = null

const stats = computed(() => queuesStore.globalStats)

const topQueues = computed(() => {
  return [...queuesStore.queues]
    .sort((a, b) => (b.stats?.pending || 0) - (a.stats?.pending || 0))
    .slice(0, 10)
})

async function fetchData() {
  loading.value = true
  try {
    await Promise.all([
      queuesStore.fetchQueues(),
      fetchThroughput(),
      fetchQueueDepths()
    ])
  } catch (error) {
    console.error('Failed to fetch dashboard data:', error)
  } finally {
    loading.value = false
  }
}

async function fetchThroughput() {
  try {
    const response = await api.analytics.getThroughput()
    // The new API returns enhanced throughput data with multiple metrics
    throughputData.value = response.throughput || []
  } catch (error) {
    console.error('Failed to fetch throughput:', error)
    throughputData.value = []
  }
}

async function fetchQueueDepths() {
  try {
    const response = await api.analytics.getQueueDepths()
    queueDepthData.value = response.depths || []
  } catch (error) {
    console.error('Failed to fetch queue depths:', error)
  }
}

function refreshQueueDepths() {
  fetchQueueDepths()
}

function getPrioritySeverity(priority) {
  if (priority >= 10) return 'danger'
  if (priority >= 5) return 'warning'
  return 'info'
}

onMounted(() => {
  fetchData()
  // Refresh data every 30 seconds
  refreshInterval = setInterval(fetchData, 30000)
})

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})
</script>

<style scoped>
.dashboard {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 1rem;
}

.charts-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1rem;
}

.queues-card {
  margin-top: 0;
}

.chart-card :deep(.p-card-content) {
  height: 300px;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.view-all {
  color: #a855f7;
  text-decoration: none;
  font-size: 0.875rem;
  display: flex;
  align-items: center;
  gap: 0.25rem;
  transition: color 0.2s;
}

.view-all:hover {
  color: #c084fc;
}

.queue-link {
  color: #a855f7;
  text-decoration: none;
}

.queue-link:hover {
  text-decoration: underline;
}

@media (max-width: 1024px) {
  .charts-row {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 768px) {
  .stats-grid {
    grid-template-columns: 1fr;
  }
}
</style>
