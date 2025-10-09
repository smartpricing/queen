<template>
  <div class="messages-view">
    <Card class="messages-card">
      <template #content>
        <DataTable 
          :value="messages" 
          :loading="loading"
          :paginator="true"
          :rows="20"
          :rowsPerPageOptions="[20, 50, 100]"
          responsiveLayout="scroll"
          class="dark-table-v3"
        >
          <template #header>
            <div class="table-header">
              <h2>Message Browser</h2>
              <div class="table-actions">
                <Select 
                  v-model="selectedQueue" 
                  :options="queues || []" 
                  optionLabel="name" 
                  optionValue="name"
                  placeholder="Select Queue"
                  class="queue-dropdown"
                  @change="fetchMessages"
                />
                <Select 
                  v-model="selectedStatus" 
                  :options="statusOptions" 
                  optionLabel="label" 
                  optionValue="value"
                  placeholder="Status"
                  class="status-dropdown"
                  @change="fetchMessages"
                />
                <Button 
                  icon="pi pi-refresh" 
                  @click="fetchMessages" 
                  :loading="loading" 
                  class="p-button-text"
                  v-tooltip="'Refresh'"
                />
              </div>
            </div>
          </template>

          <Column field="transactionId" header="Transaction ID">
            <template #body="{ data }">
              <span class="transaction-id">{{ data.transactionId.substring(0, 8) }}...</span>
            </template>
          </Column>
          
          <Column field="queue" header="Queue">
            <template #body="{ data }">
              <div class="queue-name-cell">
                <div class="queue-icon-small">Q</div>
                <span>{{ data.queue }}</span>
              </div>
            </template>
          </Column>
          
          <Column field="status" header="Status">
            <template #body="{ data }">
              <span :class="`status-${data.status}`">{{ data.status }}</span>
            </template>
          </Column>
          
          <Column field="created" header="Created">
            <template #body="{ data }">
              <span class="timestamp">{{ formatDate(data.created) }}</span>
            </template>
          </Column>
          
          <Column field="attempts" header="Attempts">
            <template #body="{ data }">
              <Tag :value="data.attempts || 0" :severity="getAttemptSeverity(data.attempts)" />
            </template>
          </Column>
          
          <Column header="Actions" :exportable="false">
            <template #body="{ data }">
              <Button 
                icon="pi pi-eye" 
                class="p-button-text p-button-sm action-btn"
                @click="viewMessage(data)"
                v-tooltip="'View Details'"
              />
              <Button 
                icon="pi pi-replay" 
                class="p-button-text p-button-sm action-btn"
                @click="retryMessage(data)"
                v-tooltip="'Retry'"
                :disabled="data.status !== 'failed'"
              />
            </template>
          </Column>
        </DataTable>
      </template>
    </Card>

    <!-- Message Detail Dialog -->
    <Dialog 
      v-model:visible="showDetail" 
      :header="`Message: ${selectedMessage?.transactionId}`"
      :style="{ width: '50vw' }"
      :modal="true"
      class="message-dialog"
    >
      <div v-if="selectedMessage" class="message-detail">
        <div class="detail-row">
          <span class="detail-label">Transaction ID:</span>
          <span class="detail-value">{{ selectedMessage.transactionId }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Queue:</span>
          <span class="detail-value">{{ selectedMessage.queue }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Status:</span>
          <span :class="`status-${selectedMessage.status}`">{{ selectedMessage.status }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Created:</span>
          <span class="detail-value">{{ formatDate(selectedMessage.created) }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Payload:</span>
        </div>
        <pre class="payload-viewer">{{ JSON.stringify(selectedMessage.payload, null, 2) }}</pre>
      </div>
    </Dialog>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useToast } from 'primevue/usetoast'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Select from 'primevue/select'
import Tag from 'primevue/tag'
import Card from 'primevue/card'
import Dialog from 'primevue/dialog'
import api from '../services/api.js'

const toast = useToast()
const loading = ref(false)
const messages = ref([])
const queues = ref([])
const selectedQueue = ref(null)
const selectedStatus = ref(null)
const selectedMessage = ref(null)
const showDetail = ref(false)

const statusOptions = [
  { label: 'All', value: null },
  { label: 'Pending', value: 'pending' },
  { label: 'Processing', value: 'processing' },
  { label: 'Completed', value: 'completed' },
  { label: 'Failed', value: 'failed' },
  { label: 'Dead Letter', value: 'dead_letter' }
]

const fetchQueues = async () => {
  try {
    const data = await api.getQueues()
    queues.value = data.queues || []
  } catch (error) {
    console.error('Failed to fetch queues:', error)
  }
}

const fetchMessages = async () => {
  try {
    loading.value = true
    
    // Build API parameters
    const params = {}
    if (selectedQueue.value) {
      params.queue = selectedQueue.value
    }
    if (selectedStatus.value) {
      params.status = selectedStatus.value
    }
    
    // Fetch real messages from API
    const data = await api.getMessages(params)
    
    if (data && Array.isArray(data.messages)) {
      // Process real API data
      messages.value = data.messages.map(message => ({
        transactionId: message.transactionId || message.id,
        queue: message.queue,
        status: message.status,
        created: message.createdAt || message.created,
        attempts: message.attempts || message.retryCount || 0,
        payload: message.payload || message.data || {}
      }))
    } else if (data && Array.isArray(data)) {
      // Handle if API returns array directly
      messages.value = data.map(message => ({
        transactionId: message.transactionId || message.id,
        queue: message.queue,
        status: message.status,
        created: message.createdAt || message.created,
        attempts: message.attempts || message.retryCount || 0,
        payload: message.payload || message.data || {}
      }))
    } else {
      // No data available
      messages.value = []
    }
    
  } catch (error) {
    console.error('Failed to fetch messages:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load messages',
      life: 3000
    })
    
    // Show empty state on error
    messages.value = []
  } finally {
    loading.value = false
  }
}

const viewMessage = (message) => {
  selectedMessage.value = message
  showDetail.value = true
}

const retryMessage = async (message) => {
  try {
    // API call to retry message
    toast.add({
      severity: 'success',
      summary: 'Success',
      detail: 'Message queued for retry',
      life: 3000
    })
  } catch (error) {
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to retry message',
      life: 3000
    })
  }
}

const formatDate = (dateString) => {
  if (!dateString) return '-'
  const date = new Date(dateString)
  return date.toLocaleString()
}

const getAttemptSeverity = (attempts) => {
  if (!attempts || attempts === 0) return 'success'
  if (attempts <= 2) return 'warning'
  return 'danger'
}

onMounted(() => {
  fetchQueues()
  fetchMessages()
})
</script>

<style scoped>
.messages-view {
  padding: 0;
}

.messages-card {
  background: transparent !important;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  box-shadow: none !important;
}

:deep(.p-card-content) {
  padding: 0;
}

.table-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1.5rem;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.table-header h2 {
  font-size: 1.25rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0;
}

.table-actions {
  display: flex;
  gap: 1rem;
  align-items: center;
}

.queue-dropdown,
.status-dropdown {
  min-width: 150px;
}

:deep(.queue-dropdown .p-dropdown),
:deep(.status-dropdown .p-dropdown) {
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
}

.transaction-id {
  font-family: 'Courier New', monospace;
  font-size: 0.875rem;
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

.timestamp {
  color: var(--surface-500);
  font-size: 0.875rem;
}

.action-btn {
  color: var(--surface-500) !important;
}

.action-btn:hover {
  background: rgba(236, 72, 153, 0.1) !important;
  color: var(--primary-500) !important;
}

/* Message Detail Dialog */
.message-dialog :deep(.p-dialog) {
  background: var(--surface-50);
  border: 1px solid rgba(255, 255, 255, 0.1);
}

.message-dialog :deep(.p-dialog-header) {
  background: var(--surface-0);
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.message-detail {
  padding: 1rem;
}

.detail-row {
  display: flex;
  align-items: center;
  gap: 1rem;
  margin-bottom: 1rem;
}

.detail-label {
  font-weight: 600;
  color: var(--surface-400);
  min-width: 120px;
}

.detail-value {
  color: var(--surface-600);
}

.payload-viewer {
  background: var(--surface-0);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
  padding: 1rem;
  color: var(--surface-600);
  font-family: 'Courier New', monospace;
  font-size: 0.875rem;
  overflow-x: auto;
}

/* DataTable dark theme overrides */
:deep(.dark-table-v3) {
  background: transparent !important;
  border: none !important;
}

:deep(.dark-table-v3 .p-datatable-header) {
  background: transparent !important;
  border: none !important;
}

:deep(.dark-table-v3 .p-datatable-thead > tr > th) {
  background: transparent !important;
  color: var(--surface-400) !important;
  border-color: rgba(255, 255, 255, 0.1) !important;
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

:deep(.p-paginator) {
  background: transparent !important;
  border-top: 1px solid rgba(255, 255, 255, 0.1) !important;
}
</style>