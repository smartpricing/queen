<template>
  <div class="queue-detail">
    <div class="detail-header">
      <div class="header-info">
        <Button 
          icon="pi pi-arrow-left" 
          class="p-button-text back-btn"
          @click="$router.push('/queues')"
          v-tooltip="'Back to Queues'"
        />
        <h1>{{ queueName }}</h1>
      </div>
      <Button 
        label="Refresh" 
        icon="pi pi-refresh" 
        class="btn-primary"
        @click="fetchQueueDetails"
        :loading="loading"
      />
    </div>

    <!-- Queue Information Card -->
    <div class="card-v3 info-card">
      <h3 class="card-title">Queue Information</h3>
      <div class="info-grid">
        <div class="info-item">
          <span class="info-label">Namespace:</span>
          <span class="info-value">{{ queueInfo.namespace || '-' }}</span>
        </div>
        <div class="info-item">
          <span class="info-label">Task:</span>
          <span class="info-value">{{ queueInfo.task || '-' }}</span>
        </div>
        <div class="info-item">
          <span class="info-label">Created:</span>
          <span class="info-value">{{ formatDate(queueInfo.created) }}</span>
        </div>
        <div class="info-item">
          <span class="info-label">Total Messages:</span>
          <span class="info-value highlight">{{ queueInfo.totalMessages || 0 }}</span>
        </div>
      </div>
    </div>

    <!-- Partitions Table -->
    <div class="card-v3">
      <h3 class="card-title">Partitions</h3>
      <DataTable 
        :value="partitions" 
        :loading="loading"
        class="dark-table-v3"
      >
        <Column field="name" header="PARTITION NAME">
          <template #body="{ data }">
            <span class="partition-name">{{ data.name }}</span>
          </template>
        </Column>
        <Column field="priority" header="PRIORITY">
          <template #body="{ data }">
            <span class="priority-value">{{ data.priority }}</span>
          </template>
        </Column>
        <Column header="STATS">
          <template #body="{ data }">
            <div class="stats-badges">
              <span class="status-pending">{{ data.pending || 0 }} pending</span>
              <span class="status-processing">{{ data.processing || 0 }} processing</span>
              <span class="status-completed">{{ data.completed || 0 }} completed</span>
            </div>
          </template>
        </Column>
      </DataTable>
    </div>

    <!-- Message Activity Chart -->
    <div class="chart-container" style="margin-top: 1.5rem;">
      <div class="chart-header">
        <h3 class="chart-title">Message Activity</h3>
        <Button 
          icon="pi pi-refresh" 
          class="p-button-text p-button-sm"
          @click="fetchQueueDetails"
          v-tooltip="'Refresh'"
        />
      </div>
      <ThroughputChart :data="activityData" :loading="loading" />
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed, watch } from 'vue'
import { useRoute } from 'vue-router'
import { useToast } from 'primevue/usetoast'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import ThroughputChart from '../components/charts/ThroughputChart.vue'
import api from '../services/api.js'

const route = useRoute()
const toast = useToast()
const loading = ref(false)

const queueName = computed(() => route.params.name || 'Unknown Queue')

const queueInfo = ref({
  namespace: null,
  task: null,
  created: null,
  totalMessages: 0
})

const partitions = ref([])

const activityData = ref({
  labels: [],
  datasets: []
})

const fetchQueueDetails = async () => {
  try {
    loading.value = true
    
    // Fetch real queue data from API
    const [queueData, analyticsData, partitionData] = await Promise.all([
      api.getQueueDetail(queueName.value).catch(() => null),
      api.getQueueAnalytics(queueName.value).catch(() => null),
      api.getPartitions({ queue: queueName.value }).catch(() => null)
    ])
    
    // Update queue info
    if (queueData) {
      queueInfo.value = {
        namespace: queueData.namespace || null,
        task: queueData.task || null,
        created: queueData.created || new Date().toISOString(),
        totalMessages: queueData.stats?.total || 
                      (queueData.stats?.pending || 0) + 
                      (queueData.stats?.processing || 0) + 
                      (queueData.stats?.completed || 0) + 
                      (queueData.stats?.failed || 0)
      }
    }
    
    // Process partition data
    if (partitionData && partitionData.partitions) {
      partitions.value = partitionData.partitions.map(p => ({
        name: p.name || p.id || 'Unknown',
        priority: p.priority || 0,
        pending: p.stats?.pending || 0,
        processing: p.stats?.processing || 0,
        completed: p.stats?.completed || 0
      }))
    } else {
      // Default partitions if no data
      partitions.value = [
        { name: 'Default', priority: 0, pending: 0, processing: 0, completed: 0 }
      ]
    }
    
    // Process throughput data for activity chart
    if (analyticsData && analyticsData.throughput && analyticsData.throughput.length > 0) {
      const recentData = analyticsData.throughput.slice(-12)
      
      activityData.value = {
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
            data: recentData.map(item => item.incoming || 0)
          },
          {
            label: 'Completed Messages',
            data: recentData.map(item => item.completed || 0)
          },
          {
            label: 'Failed Messages',
            data: recentData.map(item => item.failed || 0)
          }
        ]
      }
    } else {
      // Generate placeholder data if no real data available
      const now = new Date()
      const placeholderData = []
      for (let i = 11; i >= 0; i--) {
        const time = new Date(now.getTime() - i * 5 * 60 * 1000)
        placeholderData.push({
          timestamp: time.toISOString(),
          incoming: 0,
          completed: 0,
          failed: 0
        })
      }
      
      activityData.value = {
        labels: placeholderData.map(item => {
          const date = new Date(item.timestamp)
          return date.toLocaleTimeString('en-US', { 
            hour: 'numeric', 
            minute: '2-digit' 
          })
        }),
        datasets: [
          {
            label: 'Incoming Messages',
            data: placeholderData.map(() => 0)
          },
          {
            label: 'Completed Messages',
            data: placeholderData.map(() => 0)
          },
          {
            label: 'Failed Messages',
            data: placeholderData.map(() => 0)
          }
        ]
      }
    }
    
  } catch (error) {
    console.error('Failed to fetch queue details:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load queue details',
      life: 3000
    })
  } finally {
    loading.value = false
  }
}

const formatDate = (dateString) => {
  if (!dateString) return '-'
  const date = new Date(dateString)
  return date.toLocaleString()
}

// Watch for queue name changes
watch(() => route.params.name, (newName) => {
  if (newName) {
    fetchQueueDetails()
  }
})

onMounted(() => {
  fetchQueueDetails()
})
</script>

<style scoped>
.queue-detail {
  padding: 0;
}

.detail-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.header-info {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.header-info h1 {
  font-size: 1.5rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0;
}

.back-btn {
  color: var(--surface-500) !important;
}

.back-btn:hover {
  background: rgba(236, 72, 153, 0.1) !important;
  color: var(--primary-500) !important;
}

.card-v3 {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 1.5rem;
  position: relative;
  overflow: hidden;
  margin-bottom: 1.5rem;
}

.card-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0 0 1.5rem 0;
}

.info-card {
  margin-bottom: 1.5rem;
}

.info-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 1.5rem;
}

.info-item {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.info-label {
  font-size: 0.75rem;
  color: var(--surface-400);
  text-transform: uppercase;
  letter-spacing: 0.05em;
  font-weight: 500;
}

.info-value {
  font-size: 1rem;
  color: var(--surface-600);
  font-weight: 500;
}

.info-value.highlight {
  color: var(--primary-500);
  font-size: 1.25rem;
  font-weight: 600;
}

.partition-name {
  color: var(--surface-600);
  font-weight: 500;
}

.priority-value {
  color: var(--surface-500);
}

.stats-badges {
  display: flex;
  gap: 0.75rem;
}

.chart-container {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 1.5rem;
  height: 400px;
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

/* DataTable dark theme overrides */
:deep(.dark-table-v3) {
  background: transparent !important;
  border: none !important;
}

:deep(.dark-table-v3 .p-datatable-thead > tr > th) {
  background: transparent !important;
  color: var(--surface-400) !important;
  border-color: rgba(255, 255, 255, 0.1) !important;
  text-transform: uppercase;
  font-size: 0.75rem;
  letter-spacing: 0.05em;
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
}

/* Responsive */
@media (max-width: 768px) {
  .info-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 480px) {
  .info-grid {
    grid-template-columns: 1fr;
  }
}
</style>