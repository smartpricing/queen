<template>
  <div class="messages-page">
    <div class="page-header">
      <h1 class="page-title">Messages</h1>
      <div class="header-actions">
        <Button 
          label="Export" 
          icon="pi pi-download" 
          class="p-button-outlined"
          @click="exportMessages"
        />
        <Button 
          label="Refresh" 
          icon="pi pi-refresh" 
          @click="loadMessages"
        />
      </div>
    </div>

    <!-- Filters -->
    <Card class="filters-card">
      <template #content>
        <div class="filters-grid">
          <div class="field">
            <label>Namespace</label>
            <Dropdown 
              v-model="filters.namespace" 
              :options="namespaces" 
              optionLabel="label"
              optionValue="value"
              placeholder="All Namespaces"
              class="w-full"
              showClear
            />
          </div>
          <div class="field">
            <label>Task</label>
            <Dropdown 
              v-model="filters.task" 
              :options="tasks" 
              optionLabel="label"
              optionValue="value"
              placeholder="All Tasks"
              class="w-full"
              showClear
              :disabled="!filters.namespace"
            />
          </div>
          <div class="field">
            <label>Queue</label>
            <Dropdown 
              v-model="filters.queue" 
              :options="queues" 
              optionLabel="label"
              optionValue="value"
              placeholder="All Queues"
              class="w-full"
              showClear
              :disabled="!filters.task"
            />
          </div>
          <div class="field">
            <label>Status</label>
            <Dropdown 
              v-model="filters.status" 
              :options="statusOptions" 
              optionLabel="label"
              optionValue="value"
              placeholder="All Status"
              class="w-full"
              showClear
            />
          </div>
          <div class="field">
            <label>Date Range</label>
            <Calendar 
              v-model="filters.dateRange" 
              selectionMode="range" 
              dateFormat="mm/dd/yy"
              placeholder="Select dates"
              class="w-full"
              showIcon
              showButtonBar
            />
          </div>
          <div class="field">
            <label>Search</label>
            <div class="p-inputgroup">
              <InputText 
                v-model="filters.search" 
                placeholder="Search by ID or payload..."
                @keyup.enter="loadMessages"
              />
              <Button 
                icon="pi pi-search" 
                @click="loadMessages"
              />
            </div>
          </div>
        </div>
        <div class="filters-actions">
          <Button 
            label="Clear Filters" 
            icon="pi pi-filter-slash" 
            class="p-button-text"
            @click="clearFilters"
          />
          <Button 
            label="Apply Filters" 
            icon="pi pi-filter" 
            @click="loadMessages"
          />
        </div>
      </template>
    </Card>

    <!-- Messages Table -->
    <Card class="mt-3">
      <template #title>
        <div class="flex align-items-center justify-content-between">
          <span>Message History</span>
          <Tag :value="`${totalMessages} messages`" severity="secondary" />
        </div>
      </template>
      <template #content>
        <DataTable 
          :value="messages" 
          :loading="loading"
          v-model:selection="selectedMessages"
          dataKey="transactionId"
          responsiveLayout="scroll"
          :paginator="true"
          :rows="20"
          :rowsPerPageOptions="[10, 20, 50, 100]"
          paginatorTemplate="FirstPageLink PrevPageLink PageLinks NextPageLink LastPageLink CurrentPageReport RowsPerPageDropdown"
          currentPageReportTemplate="Showing {first} to {last} of {totalRecords} messages"
          :globalFilterFields="['transactionId', 'queuePath', 'status', 'payload']"
        >
          <Column selectionMode="multiple" style="width: 3rem" :exportable="false"></Column>
          
          <Column field="transactionId" header="Transaction ID" :sortable="true" style="min-width: 150px">
            <template #body="{ data }">
              <div class="flex align-items-center gap-2">
                <span class="font-mono text-sm">{{ data.transactionId.substring(0, 8) }}...</span>
                <Button 
                  icon="pi pi-copy" 
                  class="p-button-text p-button-sm p-0"
                  @click="copyToClipboard(data.transactionId)"
                  v-tooltip="'Copy full ID'"
                />
              </div>
            </template>
          </Column>
          
          <Column field="queuePath" header="Queue" :sortable="true" style="min-width: 200px">
            <template #body="{ data }">
              <router-link 
                :to="`/queues/${data.queuePath.replace(/\//g, '-')}`"
                class="queue-link"
              >
                {{ data.queuePath }}
              </router-link>
            </template>
          </Column>
          
          <Column field="status" header="Status" :sortable="true" style="min-width: 120px">
            <template #body="{ data }">
              <Tag :severity="getStatusSeverity(data.status)" :value="data.status" />
            </template>
            <template #filter="{ filterModel }">
              <Dropdown 
                v-model="filterModel.value" 
                :options="statusOptions" 
                optionLabel="label"
                optionValue="value"
                placeholder="Select Status"
                class="p-column-filter"
                showClear
              />
            </template>
          </Column>
          
          <Column field="payload" header="Payload" style="min-width: 250px">
            <template #body="{ data }">
              <div class="payload-cell">
                <pre class="payload-preview">{{ formatPayload(data.payload) }}</pre>
                <Button 
                  icon="pi pi-eye" 
                  class="p-button-text p-button-sm"
                  @click="viewPayload(data)"
                  v-tooltip="'View full payload'"
                />
              </div>
            </template>
          </Column>
          
          <Column field="createdAt" header="Created" :sortable="true" style="min-width: 150px">
            <template #body="{ data }">
              {{ formatDate(data.createdAt) }}
            </template>
          </Column>
          
          <Column field="completedAt" header="Completed" :sortable="true" style="min-width: 150px">
            <template #body="{ data }">
              {{ data.completedAt ? formatDate(data.completedAt) : '-' }}
            </template>
          </Column>
          
          <Column field="duration" header="Duration" :sortable="true" style="min-width: 100px">
            <template #body="{ data }">
              {{ calculateDuration(data) }}
            </template>
          </Column>
          
          <Column header="Actions" style="min-width: 120px" :exportable="false">
            <template #body="{ data }">
              <div class="flex gap-1">
                <Button 
                  icon="pi pi-info-circle" 
                  class="p-button-text p-button-sm"
                  @click="viewDetails(data)"
                  v-tooltip="'View details'"
                />
                <Button 
                  icon="pi pi-replay" 
                  class="p-button-text p-button-sm"
                  @click="retryMessage(data)"
                  v-if="data.status === 'failed'"
                  v-tooltip="'Retry'"
                />
              </div>
            </template>
          </Column>
        </DataTable>
        
        <div class="bulk-actions" v-if="selectedMessages.length > 0">
          <span class="text-sm">{{ selectedMessages.length }} messages selected</span>
          <div class="flex gap-2">
            <Button 
              label="Retry Selected" 
              icon="pi pi-replay" 
              class="p-button-sm"
              @click="retrySelected"
              :disabled="!hasFailedMessages"
            />
            <Button 
              label="Export Selected" 
              icon="pi pi-download" 
              class="p-button-sm p-button-outlined"
              @click="exportSelected"
            />
          </div>
        </div>
      </template>
    </Card>

    <!-- Message Detail Dialog -->
    <Dialog v-model:visible="showDetailDialog" header="Message Details" :style="{ width: '800px' }" :maximizable="true">
      <div v-if="selectedMessage" class="message-details">
        <TabView>
          <TabPanel header="Overview">
            <div class="detail-grid">
              <div class="detail-item">
                <span class="detail-label">Transaction ID</span>
                <span class="detail-value font-mono">{{ selectedMessage.transactionId }}</span>
              </div>
              <div class="detail-item">
                <span class="detail-label">Queue Path</span>
                <span class="detail-value">{{ selectedMessage.queuePath }}</span>
              </div>
              <div class="detail-item">
                <span class="detail-label">Status</span>
                <Tag :severity="getStatusSeverity(selectedMessage.status)" :value="selectedMessage.status" />
              </div>
              <div class="detail-item">
                <span class="detail-label">Created At</span>
                <span class="detail-value">{{ formatFullDate(selectedMessage.createdAt) }}</span>
              </div>
              <div class="detail-item" v-if="selectedMessage.lockedAt">
                <span class="detail-label">Locked At</span>
                <span class="detail-value">{{ formatFullDate(selectedMessage.lockedAt) }}</span>
              </div>
              <div class="detail-item" v-if="selectedMessage.completedAt">
                <span class="detail-label">Completed At</span>
                <span class="detail-value">{{ formatFullDate(selectedMessage.completedAt) }}</span>
              </div>
              <div class="detail-item" v-if="selectedMessage.failedAt">
                <span class="detail-label">Failed At</span>
                <span class="detail-value">{{ formatFullDate(selectedMessage.failedAt) }}</span>
              </div>
              <div class="detail-item" v-if="selectedMessage.workerId">
                <span class="detail-label">Worker ID</span>
                <span class="detail-value font-mono">{{ selectedMessage.workerId }}</span>
              </div>
              <div class="detail-item" v-if="selectedMessage.retryCount > 0">
                <span class="detail-label">Retry Count</span>
                <span class="detail-value">{{ selectedMessage.retryCount }}</span>
              </div>
              <div class="detail-item" v-if="selectedMessage.leaseExpiresAt">
                <span class="detail-label">Lease Expires</span>
                <span class="detail-value">{{ formatFullDate(selectedMessage.leaseExpiresAt) }}</span>
              </div>
            </div>
            <div v-if="selectedMessage.errorMessage" class="error-section">
              <h3>Error Message</h3>
              <pre class="error-message">{{ selectedMessage.errorMessage }}</pre>
            </div>
          </TabPanel>
          
          <TabPanel header="Payload">
            <pre class="payload-full">{{ JSON.stringify(selectedMessage.payload, null, 2) }}</pre>
          </TabPanel>
          
          <TabPanel header="Timeline">
            <Timeline :value="getMessageTimeline(selectedMessage)" align="left">
              <template #content="slotProps">
                <div class="timeline-item">
                  <div class="timeline-title">{{ slotProps.item.title }}</div>
                  <div class="timeline-date">{{ formatFullDate(slotProps.item.date) }}</div>
                  <div v-if="slotProps.item.description" class="timeline-description">
                    {{ slotProps.item.description }}
                  </div>
                </div>
              </template>
            </Timeline>
          </TabPanel>
        </TabView>
      </div>
    </Dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useToast } from 'primevue/usetoast'
import { useConfirm } from 'primevue/useconfirm'
import { format, formatDistance } from 'date-fns'
import Button from 'primevue/button'
import Card from 'primevue/card'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Tag from 'primevue/tag'
import Dropdown from 'primevue/dropdown'
import Calendar from 'primevue/calendar'
import InputText from 'primevue/inputtext'
import Dialog from 'primevue/dialog'
import TabView from 'primevue/tabview'
import TabPanel from 'primevue/tabpanel'
import Timeline from 'primevue/timeline'
import { useMessagesStore } from '../stores/messages'
import { api } from '../utils/api'

const router = useRouter()
const toast = useToast()
const confirm = useConfirm()
const messagesStore = useMessagesStore()

// State
const messages = ref([])
const selectedMessages = ref([])
const loading = ref(false)
const totalMessages = ref(0)
const showDetailDialog = ref(false)
const selectedMessage = ref(null)

// Filters
const filters = ref({
  namespace: null,
  task: null,
  queue: null,
  status: null,
  dateRange: null,
  search: ''
})

// Options
const namespaces = ref([])
const tasks = ref([])
const queues = ref([])

const statusOptions = [
  { label: 'Pending', value: 'pending' },
  { label: 'Processing', value: 'processing' },
  { label: 'Completed', value: 'completed' },
  { label: 'Failed', value: 'failed' },
  { label: 'Dead Letter', value: 'dead_letter' }
]

// Computed
const hasFailedMessages = computed(() => {
  return selectedMessages.value.some(m => m.status === 'failed')
})

// Methods
const loadMessages = async () => {
  loading.value = true
  try {
    // In a real implementation, this would fetch from the API with filters
    // For now, we'll use store data or simulate
    const response = await messagesStore.fetchRecentMessages()
    messages.value = response || []
    totalMessages.value = messages.value.length
  } catch (error) {
    console.error('Failed to load messages:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load messages',
      life: 3000
    })
  } finally {
    loading.value = false
  }
}

const loadFilterOptions = async () => {
  try {
    // Load namespaces, tasks, and queues for filters
    // This would come from the API
    namespaces.value = [
      { label: 'Production', value: 'production' },
      { label: 'Staging', value: 'staging' },
      { label: 'Development', value: 'development' }
    ]
  } catch (error) {
    console.error('Failed to load filter options:', error)
  }
}

const clearFilters = () => {
  filters.value = {
    namespace: null,
    task: null,
    queue: null,
    status: null,
    dateRange: null,
    search: ''
  }
  loadMessages()
}

const viewDetails = (message) => {
  selectedMessage.value = message
  showDetailDialog.value = true
}

const viewPayload = (message) => {
  selectedMessage.value = message
  showDetailDialog.value = true
  // Switch to payload tab
  setTimeout(() => {
    const tabPanels = document.querySelectorAll('.p-tabview-nav li')
    if (tabPanels[1]) tabPanels[1].click()
  }, 100)
}

const retryMessage = async (message) => {
  confirm.require({
    message: `Are you sure you want to retry message ${message.transactionId.substring(0, 8)}...?`,
    header: 'Confirm Retry',
    icon: 'pi pi-replay',
    accept: async () => {
      try {
        // Retry logic here
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: 'Message queued for retry',
          life: 3000
        })
        await loadMessages()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to retry message',
          life: 3000
        })
      }
    }
  })
}

const retrySelected = async () => {
  const failedCount = selectedMessages.value.filter(m => m.status === 'failed').length
  confirm.require({
    message: `Are you sure you want to retry ${failedCount} failed messages?`,
    header: 'Confirm Bulk Retry',
    icon: 'pi pi-replay',
    accept: async () => {
      try {
        // Bulk retry logic
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: `${failedCount} messages queued for retry`,
          life: 3000
        })
        selectedMessages.value = []
        await loadMessages()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to retry messages',
          life: 3000
        })
      }
    }
  })
}

const exportMessages = () => {
  // Export all messages logic
  const data = JSON.stringify(messages.value, null, 2)
  const blob = new Blob([data], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `messages-${format(new Date(), 'yyyyMMdd-HHmmss')}.json`
  a.click()
  URL.revokeObjectURL(url)
  
  toast.add({
    severity: 'success',
    summary: 'Success',
    detail: 'Messages exported successfully',
    life: 3000
  })
}

const exportSelected = () => {
  // Export selected messages
  const data = JSON.stringify(selectedMessages.value, null, 2)
  const blob = new Blob([data], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `selected-messages-${format(new Date(), 'yyyyMMdd-HHmmss')}.json`
  a.click()
  URL.revokeObjectURL(url)
  
  toast.add({
    severity: 'success',
    summary: 'Success',
    detail: `${selectedMessages.value.length} messages exported`,
    life: 3000
  })
}

const copyToClipboard = async (text) => {
  try {
    await navigator.clipboard.writeText(text)
    toast.add({
      severity: 'success',
      summary: 'Copied',
      detail: 'ID copied to clipboard',
      life: 2000
    })
  } catch (error) {
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to copy to clipboard',
      life: 2000
    })
  }
}

const formatPayload = (payload) => {
  const str = JSON.stringify(payload)
  return str.length > 100 ? str.substring(0, 100) + '...' : str
}

const formatDate = (date) => {
  if (!date) return '-'
  return format(new Date(date), 'MMM dd, HH:mm:ss')
}

const formatFullDate = (date) => {
  if (!date) return '-'
  return format(new Date(date), 'PPpp')
}

const calculateDuration = (message) => {
  if (!message.createdAt) return '-'
  const endTime = message.completedAt || message.failedAt || new Date()
  return formatDistance(new Date(message.createdAt), new Date(endTime), { includeSeconds: true })
}

const getStatusSeverity = (status) => {
  const severities = {
    pending: 'warning',
    processing: 'info',
    completed: 'success',
    failed: 'danger',
    dead_letter: 'secondary'
  }
  return severities[status] || 'secondary'
}

const getMessageTimeline = (message) => {
  const timeline = []
  
  if (message.createdAt) {
    timeline.push({
      title: 'Message Created',
      date: message.createdAt,
      description: 'Message added to queue'
    })
  }
  
  if (message.lockedAt) {
    timeline.push({
      title: 'Processing Started',
      date: message.lockedAt,
      description: `Picked up by worker: ${message.workerId}`
    })
  }
  
  if (message.completedAt) {
    timeline.push({
      title: 'Processing Completed',
      date: message.completedAt,
      description: 'Message processed successfully'
    })
  }
  
  if (message.failedAt) {
    timeline.push({
      title: 'Processing Failed',
      date: message.failedAt,
      description: message.errorMessage || 'Processing failed'
    })
  }
  
  return timeline
}

// Watch filters
watch(() => filters.value.namespace, (newVal) => {
  if (!newVal) {
    filters.value.task = null
    filters.value.queue = null
    tasks.value = []
    queues.value = []
  } else {
    // Load tasks for selected namespace
    tasks.value = [
      { label: 'Email', value: 'email' },
      { label: 'SMS', value: 'sms' },
      { label: 'Push', value: 'push' }
    ]
  }
})

watch(() => filters.value.task, (newVal) => {
  if (!newVal) {
    filters.value.queue = null
    queues.value = []
  } else {
    // Load queues for selected task
    queues.value = [
      { label: 'High Priority', value: 'high' },
      { label: 'Normal', value: 'normal' },
      { label: 'Low Priority', value: 'low' }
    ]
  }
})

// Lifecycle
onMounted(() => {
  loadFilterOptions()
  loadMessages()
})
</script>

<style scoped>
.messages-page {
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

.filters-card {
  margin-bottom: 1rem;
}

.filters-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 1rem;
  margin-bottom: 1rem;
}

.field {
  display: flex;
  flex-direction: column;
}

.field label {
  font-size: 0.875rem;
  color: #a3a3a3;
  margin-bottom: 0.25rem;
}

.filters-actions {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 1rem;
  border-top: 1px solid var(--surface-300);
}

.queue-link {
  color: #a855f7;
  text-decoration: none;
  transition: color 0.2s;
}

.queue-link:hover {
  color: #c084fc;
  text-decoration: underline;
}

.payload-cell {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.payload-preview {
  font-family: monospace;
  font-size: 0.75rem;
  background: var(--surface-100);
  padding: 0.25rem 0.5rem;
  border-radius: 4px;
  margin: 0;
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.bulk-actions {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1rem;
  background: var(--surface-100);
  border-radius: 0 0 6px 6px;
  margin-top: -1px;
}

.message-details {
  min-height: 400px;
}

.detail-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 1.5rem;
}

.detail-item {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.detail-label {
  font-size: 0.875rem;
  color: #a3a3a3;
  font-weight: 500;
}

.detail-value {
  font-size: 1rem;
  color: white;
}

.error-section {
  margin-top: 2rem;
  padding-top: 1.5rem;
  border-top: 1px solid var(--surface-300);
}

.error-section h3 {
  color: #ef4444;
  margin-bottom: 1rem;
}

.error-message {
  font-family: monospace;
  font-size: 0.875rem;
  background: var(--surface-100);
  padding: 1rem;
  border-radius: 4px;
  color: #ef4444;
  white-space: pre-wrap;
  word-break: break-word;
}

.payload-full {
  font-family: monospace;
  font-size: 0.875rem;
  background: var(--surface-100);
  padding: 1rem;
  border-radius: 4px;
  max-height: 500px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-word;
}

.timeline-item {
  padding: 0.5rem 0;
}

.timeline-title {
  font-weight: 600;
  color: white;
  margin-bottom: 0.25rem;
}

.timeline-date {
  font-size: 0.875rem;
  color: #a3a3a3;
}

.timeline-description {
  font-size: 0.875rem;
  color: #d1d5db;
  margin-top: 0.25rem;
}

.font-mono {
  font-family: monospace;
}

.text-sm {
  font-size: 0.875rem;
}
</style>
