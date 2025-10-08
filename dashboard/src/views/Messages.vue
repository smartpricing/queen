<template>
  <div class="messages-view">
    <div class="view-header">
      <h1>Message Browser</h1>
      <Button label="Refresh" icon="pi pi-refresh" @click="fetchMessages" :loading="loading" />
    </div>

    <Card>
      <template #content>
        <div class="filters">
        <Dropdown 
          v-model="filters.queue" 
          :options="queueOptions" 
          placeholder="Select Queue"
          showClear
          style="width: 200px"
        />
        <Dropdown 
          v-model="filters.status" 
          :options="statusOptions" 
          placeholder="Select Status"
          showClear
          style="width: 200px"
        />
        <Button label="Apply Filters" icon="pi pi-filter" @click="fetchMessages" />
      </div>

      <DataTable 
        :value="messages" 
        :loading="loading"
        :paginator="true"
        :rows="20"
        responsiveLayout="scroll"
      >
        <Column field="transactionId" header="Transaction ID" style="width: 200px">
          <template #body="{ data }">
            <span class="monospace">{{ data.transactionId.substring(0, 12) }}...</span>
          </template>
        </Column>
        
        <Column field="queue" header="Queue" />
        
        <Column field="partition" header="Partition" />
        
        <Column field="status" header="Status">
          <template #body="{ data }">
            <Tag :value="data.status" :severity="getStatusSeverity(data.status)" />
          </template>
        </Column>
        
        <Column field="createdAt" header="Created">
          <template #body="{ data }">
            {{ formatDate(data.createdAt, 'short') }}
          </template>
        </Column>
        
        <Column field="retryCount" header="Retries">
          <template #body="{ data }">
            <Badge :value="data.retryCount" :severity="data.retryCount > 0 ? 'warn' : 'secondary'" />
          </template>
        </Column>
        
        <Column header="Actions" style="width: 150px">
          <template #body="{ data }">
            <Button 
              icon="pi pi-eye" 
              class="p-button-text p-button-sm"
              @click="viewMessage(data)"
              v-tooltip="'View Details'"
            />
            <Button 
              v-if="data.status === 'failed'"
              icon="pi pi-refresh" 
              class="p-button-text p-button-sm p-button-warning"
              @click="retryMessage(data)"
              v-tooltip="'Retry'"
            />
          </template>
        </Column>
      </DataTable>
      </template>
    </Card>

    <!-- Message Detail Dialog -->
    <Dialog 
      v-model:visible="showMessageDialog" 
      header="Message Details" 
      :modal="true"
      :style="{ width: '600px' }"
    >
      <div v-if="selectedMessage" class="message-details">
        <div class="detail-item">
          <span class="detail-label">Transaction ID:</span>
          <span class="monospace">{{ selectedMessage.transactionId }}</span>
        </div>
        <div class="detail-item">
          <span class="detail-label">Queue/Partition:</span>
          <span>{{ selectedMessage.queue }}/{{ selectedMessage.partition }}</span>
        </div>
        <div class="detail-item">
          <span class="detail-label">Status:</span>
          <Tag :value="selectedMessage.status" :severity="getStatusSeverity(selectedMessage.status)" />
        </div>
        <div class="detail-item">
          <span class="detail-label">Payload:</span>
          <pre class="payload-display">{{ JSON.stringify(selectedMessage.payload, null, 2) }}</pre>
        </div>
      </div>
    </Dialog>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useToast } from 'primevue/usetoast'
import Card from 'primevue/card'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Dropdown from 'primevue/dropdown'
import Tag from 'primevue/tag'
import Badge from 'primevue/badge'
import Dialog from 'primevue/dialog'

import api from '../services/api.js'
import { formatDate, getStatusSeverity } from '../utils/helpers.js'

const toast = useToast()
const loading = ref(false)
const messages = ref([])
const filters = ref({
  queue: null,
  status: null
})

const queueOptions = ref([])
const statusOptions = ref([
  'pending',
  'processing',
  'completed',
  'failed',
  'dead_letter'
])

const showMessageDialog = ref(false)
const selectedMessage = ref(null)

const fetchMessages = async () => {
  try {
    loading.value = true
    const params = {}
    if (filters.value.queue) params.queue = filters.value.queue
    if (filters.value.status) params.status = filters.value.status
    
    const data = await api.getMessages(params)
    messages.value = data.messages || []
  } catch (error) {
    console.error('Failed to fetch messages:', error)
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

const fetchQueues = async () => {
  try {
    const data = await api.getQueues()
    queueOptions.value = data.queues?.map(q => q.name) || []
  } catch (error) {
    console.error('Failed to fetch queues:', error)
  }
}

const viewMessage = (message) => {
  selectedMessage.value = message
  showMessageDialog.value = true
}

const retryMessage = async (message) => {
  try {
    await api.retryMessage(message.transactionId)
    toast.add({
      severity: 'success',
      summary: 'Success',
      detail: 'Message queued for retry',
      life: 3000
    })
    fetchMessages()
  } catch (error) {
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to retry message',
      life: 3000
    })
  }
}

onMounted(() => {
  fetchMessages()
  fetchQueues()
})
</script>

<style scoped>
.messages-view {
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

.filters {
  display: flex;
  gap: 1rem;
  margin-bottom: 1.5rem;
  padding: 1rem;
  background: var(--gray-50);
  border-radius: var(--radius-md);
}

.monospace {
  font-family: 'Courier New', monospace;
  font-size: 0.875rem;
}

.message-details {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.detail-item {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.detail-label {
  font-weight: 600;
  color: var(--gray-700);
}

.payload-display {
  background: var(--gray-50);
  padding: 1rem;
  border-radius: var(--radius-md);
  overflow-x: auto;
  font-size: 0.875rem;
}
</style>
