<template>
  <div class="activity-view">
    <!-- Header -->
    <div class="view-header">
      <div class="header-left">
        <h2>Activity Monitor</h2>
        <Tag severity="success" v-if="isConnected">
          <i class="pi pi-circle-fill mr-1" style="font-size: 0.5rem"></i>
          Live
        </Tag>
        <Tag severity="danger" v-else>
          <i class="pi pi-circle mr-1" style="font-size: 0.5rem"></i>
          Disconnected
        </Tag>
      </div>
      <div class="header-actions">
        <Button 
          label="Clear" 
          icon="pi pi-trash" 
          class="p-button-text p-button-sm"
          @click="clearActivity"
          :disabled="recentActivity.length === 0"
        />
        <Button 
          label="Export" 
          icon="pi pi-download" 
          class="p-button-text p-button-sm"
          @click="exportActivity"
          :disabled="recentActivity.length === 0"
        />
      </div>
    </div>

    <!-- Filters -->
    <Card class="filters-card">
      <template #content>
        <div class="filters">
          <div class="filter-group">
            <label>Event Type</label>
            <MultiSelect 
              v-model="selectedTypes" 
              :options="eventTypes"
              optionLabel="label"
              optionValue="value"
              placeholder="All Types"
              class="w-full md:w-20rem"
              display="chip"
            />
          </div>
          <div class="filter-group">
            <label>Time Range</label>
            <Dropdown 
              v-model="timeRange" 
              :options="timeRanges"
              optionLabel="label"
              optionValue="value"
              placeholder="Select time range"
              class="w-full md:w-15rem"
            />
          </div>
          <div class="filter-group">
            <label>Search</label>
            <span class="p-input-icon-left w-full md:w-20rem">
              <i class="pi pi-search" />
              <InputText 
                v-model="searchQuery" 
                placeholder="Search activity..." 
                class="w-full"
              />
            </span>
          </div>
        </div>
      </template>
    </Card>

    <!-- Activity Stats -->
    <div class="stats-row">
      <Card class="stat-card">
        <template #content>
          <div class="stat-content">
            <span class="stat-label">Total Events</span>
            <span class="stat-value">{{ filteredActivity.length }}</span>
          </div>
        </template>
      </Card>
      <Card class="stat-card">
        <template #content>
          <div class="stat-content">
            <span class="stat-label">Messages Pushed</span>
            <span class="stat-value">{{ messagesPushed }}</span>
          </div>
        </template>
      </Card>
      <Card class="stat-card">
        <template #content>
          <div class="stat-content">
            <span class="stat-label">Messages Popped</span>
            <span class="stat-value">{{ messagesPopped }}</span>
          </div>
        </template>
      </Card>
      <Card class="stat-card">
        <template #content>
          <div class="stat-content">
            <span class="stat-label">Acknowledgments</span>
            <span class="stat-value">{{ acknowledgments }}</span>
          </div>
        </template>
      </Card>
    </div>

    <!-- Activity Feed -->
    <Card class="activity-card">
      <template #title>
        <div class="card-header">
          <span>Activity Feed</span>
          <span class="activity-count">{{ filteredActivity.length }} events</span>
        </div>
      </template>
      <template #content>
        <div v-if="filteredActivity.length === 0" class="empty-state">
          <i class="pi pi-inbox text-6xl text-gray-500"></i>
          <h3>No activity found</h3>
          <p>{{ searchQuery ? 'Try adjusting your search criteria' : 'Activity will appear here as events occur' }}</p>
        </div>
        
        <DataTable 
          v-else
          :value="filteredActivity" 
          :paginator="true"
          :rows="20"
          :rowsPerPageOptions="[10, 20, 50, 100]"
          responsiveLayout="scroll"
          class="activity-table"
          :globalFilterFields="['type', 'queue', 'status', 'data']"
        >
          <Column field="timestamp" header="Time" :sortable="true" style="width: 180px">
            <template #body="{ data }">
              <div class="timestamp-cell">
                <span class="time-absolute">{{ formatDateTime(data.timestamp) }}</span>
                <span class="time-relative">{{ formatTimeAgo(data.timestamp) }}</span>
              </div>
            </template>
          </Column>
          
          <Column field="type" header="Type" :sortable="true" style="width: 150px">
            <template #body="{ data }">
              <div class="type-cell">
                <i :class="getEventIcon(data.type)" class="mr-2"></i>
                <Tag :severity="getEventSeverity(data.type)">
                  {{ formatEventType(data.type) }}
                </Tag>
              </div>
            </template>
          </Column>
          
          <Column field="details" header="Details">
            <template #body="{ data }">
              <div class="details-cell">
                <template v-if="data.type === 'push'">
                  <i class="pi pi-upload text-blue-500 mr-2"></i>
                  <span>Pushed {{ data.count }} message{{ data.count !== 1 ? 's' : '' }}</span>
                </template>
                <template v-else-if="data.type === 'pop'">
                  <i class="pi pi-download text-green-500 mr-2"></i>
                  <span>Popped {{ data.count }} message{{ data.count !== 1 ? 's' : '' }} from </span>
                  <Tag severity="info">{{ data.queue }}</Tag>
                </template>
                <template v-else-if="data.type === 'ack'">
                  <i class="pi pi-check text-purple-500 mr-2"></i>
                  <span>Acknowledgment: </span>
                  <Tag :severity="data.status === 'completed' ? 'success' : 'danger'">
                    {{ data.status }}
                  </Tag>
                  <span class="text-sm text-gray-500 ml-2">{{ data.transactionId?.substring(0, 8) }}...</span>
                </template>
                <template v-else-if="data.data">
                  <span class="text-sm">
                    {{ formatDataPreview(data.data) }}
                  </span>
                </template>
                <template v-else>
                  <span class="text-gray-500">No additional details</span>
                </template>
              </div>
            </template>
          </Column>
          
          <Column header="Actions" style="width: 100px">
            <template #body="{ data }">
              <Button 
                icon="pi pi-eye" 
                class="p-button-text p-button-sm"
                @click="viewDetails(data)"
                v-tooltip="'View Details'"
              />
            </template>
          </Column>
        </DataTable>
      </template>
    </Card>

    <!-- Details Dialog -->
    <Dialog 
      v-model:visible="showDetailsDialog" 
      :header="'Activity Details'" 
      :modal="true"
      :style="{ width: '50vw' }"
    >
      <div v-if="selectedActivity" class="activity-details">
        <div class="detail-row">
          <span class="detail-label">Type:</span>
          <Tag :severity="getEventSeverity(selectedActivity.type)">
            {{ formatEventType(selectedActivity.type) }}
          </Tag>
        </div>
        <div class="detail-row">
          <span class="detail-label">Timestamp:</span>
          <span>{{ formatDateTime(selectedActivity.timestamp) }}</span>
        </div>
        <div class="detail-row" v-if="selectedActivity.queue">
          <span class="detail-label">Queue:</span>
          <span>{{ selectedActivity.queue }}</span>
        </div>
        <div class="detail-row" v-if="selectedActivity.count">
          <span class="detail-label">Count:</span>
          <span>{{ selectedActivity.count }}</span>
        </div>
        <div class="detail-row" v-if="selectedActivity.status">
          <span class="detail-label">Status:</span>
          <Tag :severity="selectedActivity.status === 'completed' ? 'success' : 'danger'">
            {{ selectedActivity.status }}
          </Tag>
        </div>
        <div class="detail-row" v-if="selectedActivity.transactionId">
          <span class="detail-label">Transaction ID:</span>
          <span class="monospace">{{ selectedActivity.transactionId }}</span>
        </div>
        <div class="detail-row" v-if="selectedActivity.data">
          <span class="detail-label">Data:</span>
          <pre class="detail-data">{{ JSON.stringify(selectedActivity.data, null, 2) }}</pre>
        </div>
      </div>
    </Dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import Card from 'primevue/card'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Tag from 'primevue/tag'
import Dialog from 'primevue/dialog'
import MultiSelect from 'primevue/multiselect'
import Dropdown from 'primevue/dropdown'
import InputText from 'primevue/inputtext'
import { formatDistanceToNow, format, subHours, isAfter } from 'date-fns'

import { useMessagesStore } from '../stores/messages'
import { useWebSocket } from '../composables/useWebSocket'

const messagesStore = useMessagesStore()
const { isConnected } = useWebSocket()

const searchQuery = ref('')
const selectedTypes = ref([])
const timeRange = ref('all')
const showDetailsDialog = ref(false)
const selectedActivity = ref(null)

const eventTypes = [
  { label: 'Push', value: 'push' },
  { label: 'Pop', value: 'pop' },
  { label: 'Acknowledgment', value: 'ack' },
  { label: 'Processing', value: 'processing' },
  { label: 'Completed', value: 'completed' },
  { label: 'Failed', value: 'failed' }
]

const timeRanges = [
  { label: 'All Time', value: 'all' },
  { label: 'Last Hour', value: '1h' },
  { label: 'Last 6 Hours', value: '6h' },
  { label: 'Last 24 Hours', value: '24h' },
  { label: 'Last 7 Days', value: '7d' }
]

const recentActivity = computed(() => messagesStore.recentActivity)

const filteredActivity = computed(() => {
  let filtered = [...recentActivity.value]
  
  // Filter by type
  if (selectedTypes.value.length > 0) {
    filtered = filtered.filter(item => 
      selectedTypes.value.some(type => item.type?.includes(type))
    )
  }
  
  // Filter by time range
  if (timeRange.value !== 'all') {
    const now = new Date()
    let cutoffTime
    
    switch (timeRange.value) {
      case '1h':
        cutoffTime = subHours(now, 1)
        break
      case '6h':
        cutoffTime = subHours(now, 6)
        break
      case '24h':
        cutoffTime = subHours(now, 24)
        break
      case '7d':
        cutoffTime = subHours(now, 24 * 7)
        break
    }
    
    if (cutoffTime) {
      filtered = filtered.filter(item => {
        const itemTime = item.timestamp instanceof Date ? item.timestamp : new Date(item.timestamp)
        return isAfter(itemTime, cutoffTime)
      })
    }
  }
  
  // Filter by search query
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase()
    filtered = filtered.filter(item => {
      const searchableText = [
        item.type,
        item.queue,
        item.status,
        item.transactionId,
        JSON.stringify(item.data)
      ].filter(Boolean).join(' ').toLowerCase()
      
      return searchableText.includes(query)
    })
  }
  
  return filtered
})

const messagesPushed = computed(() => {
  return filteredActivity.value
    .filter(a => a.type === 'push')
    .reduce((sum, a) => sum + (a.count || 0), 0)
})

const messagesPopped = computed(() => {
  return filteredActivity.value
    .filter(a => a.type === 'pop')
    .reduce((sum, a) => sum + (a.count || 0), 0)
})

const acknowledgments = computed(() => {
  return filteredActivity.value.filter(a => a.type === 'ack').length
})

function getEventSeverity(type) {
  if (type?.includes('completed') || type === 'pop') return 'success'
  if (type?.includes('failed')) return 'danger'
  if (type?.includes('processing')) return 'warning'
  if (type === 'push') return 'info'
  if (type === 'ack') return 'secondary'
  return 'secondary'
}

function getEventIcon(type) {
  if (type?.includes('push')) return 'pi pi-upload'
  if (type?.includes('pop')) return 'pi pi-download'
  if (type?.includes('completed')) return 'pi pi-check-circle'
  if (type?.includes('failed')) return 'pi pi-times-circle'
  if (type?.includes('processing')) return 'pi pi-spin pi-spinner'
  if (type?.includes('ack')) return 'pi pi-check'
  return 'pi pi-circle'
}

function formatEventType(type) {
  if (!type) return 'Unknown'
  return type.replace(/[._]/g, ' ').replace(/\b\w/g, l => l.toUpperCase())
}

function formatDateTime(timestamp) {
  if (!timestamp) return ''
  const date = timestamp instanceof Date ? timestamp : new Date(timestamp)
  return format(date, 'MMM dd, HH:mm:ss')
}

function formatTimeAgo(timestamp) {
  if (!timestamp) return ''
  const date = timestamp instanceof Date ? timestamp : new Date(timestamp)
  return formatDistanceToNow(date, { addSuffix: true })
}

function formatDataPreview(data) {
  if (!data) return ''
  const str = typeof data === 'string' ? data : JSON.stringify(data)
  return str.length > 100 ? str.substring(0, 100) + '...' : str
}

function clearActivity() {
  messagesStore.recentActivity = []
}

function exportActivity() {
  const data = filteredActivity.value.map(item => ({
    timestamp: item.timestamp,
    type: item.type,
    queue: item.queue,
    count: item.count,
    status: item.status,
    transactionId: item.transactionId,
    data: item.data
  }))
  
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `activity-${format(new Date(), 'yyyy-MM-dd-HHmmss')}.json`
  a.click()
  URL.revokeObjectURL(url)
}

function viewDetails(activity) {
  selectedActivity.value = activity
  showDetailsDialog.value = true
}

// Auto-refresh
let refreshInterval = null

onMounted(() => {
  // Refresh display every 10 seconds to update relative times
  refreshInterval = setInterval(() => {
    // Force re-render by triggering a computed property update
  }, 10000)
})

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})
</script>

<style scoped>
.activity-view {
  padding: 1.5rem;
  max-width: 1400px;
  margin: 0 auto;
}

.view-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1.5rem;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.header-left h2 {
  margin: 0;
  font-size: 1.5rem;
  font-weight: 600;
}

.header-actions {
  display: flex;
  gap: 0.5rem;
}

.filters-card {
  margin-bottom: 1.5rem;
}

.filters {
  display: flex;
  gap: 1.5rem;
  flex-wrap: wrap;
}

.filter-group {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.filter-group label {
  font-size: 0.875rem;
  font-weight: 500;
  color: #737373;
}

.stats-row {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 1rem;
  margin-bottom: 1.5rem;
}

.stat-card :deep(.p-card-body) {
  padding: 1rem;
}

.stat-content {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.stat-label {
  font-size: 0.875rem;
  color: #737373;
}

.stat-value {
  font-size: 1.5rem;
  font-weight: 600;
  color: #a855f7;
}

.activity-card {
  margin-bottom: 1.5rem;
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.activity-count {
  font-size: 0.875rem;
  color: #737373;
  font-weight: normal;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 3rem;
  text-align: center;
}

.empty-state h3 {
  margin: 1rem 0 0.5rem;
  color: #d1d5db;
}

.empty-state p {
  color: #737373;
  margin: 0;
}

.activity-table :deep(.p-datatable-tbody) {
  font-size: 0.875rem;
}

.timestamp-cell {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.time-absolute {
  font-weight: 500;
}

.time-relative {
  font-size: 0.75rem;
  color: #737373;
}

.type-cell {
  display: flex;
  align-items: center;
}

.details-cell {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0.5rem;
}

.activity-details {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.detail-row {
  display: flex;
  gap: 1rem;
  align-items: flex-start;
}

.detail-label {
  font-weight: 500;
  color: #737373;
  min-width: 120px;
}

.monospace {
  font-family: monospace;
  font-size: 0.875rem;
}

.detail-data {
  background: var(--surface-100);
  padding: 1rem;
  border-radius: 4px;
  font-size: 0.875rem;
  max-height: 300px;
  overflow: auto;
  margin: 0;
  flex: 1;
}

@media (max-width: 768px) {
  .view-header {
    flex-direction: column;
    align-items: flex-start;
    gap: 1rem;
  }
  
  .filters {
    flex-direction: column;
  }
  
  .filter-group {
    width: 100%;
  }
  
  .stats-row {
    grid-template-columns: 1fr 1fr;
  }
}
</style>
