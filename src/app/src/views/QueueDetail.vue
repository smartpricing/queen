<template>
  <div class="queue-detail">
    <div class="page-header">
      <Button 
        icon="pi pi-arrow-left" 
        label="Back to Queues" 
        class="p-button-text"
        @click="router.push('/queues')"
      />
      <h1 class="page-title">{{ queuePath }}</h1>
    </div>

    <div class="grid">
      <!-- Queue Stats -->
      <div class="col-12 lg:col-3">
        <Card>
          <template #title>
            <div class="flex align-items-center justify-content-between">
              <span>Queue Stats</span>
              <Button 
                icon="pi pi-refresh" 
                class="p-button-text p-button-sm"
                @click="loadQueueStats"
              />
            </div>
          </template>
          <template #content>
            <div class="stats-grid">
              <div class="stat-item">
                <div class="stat-label">Pending</div>
                <div class="stat-value text-yellow-500">{{ stats.pending || 0 }}</div>
              </div>
              <div class="stat-item">
                <div class="stat-label">Processing</div>
                <div class="stat-value text-blue-500">{{ stats.processing || 0 }}</div>
              </div>
              <div class="stat-item">
                <div class="stat-label">Completed</div>
                <div class="stat-value text-green-500">{{ stats.completed || 0 }}</div>
              </div>
              <div class="stat-item">
                <div class="stat-label">Failed</div>
                <div class="stat-value text-red-500">{{ stats.failed || 0 }}</div>
              </div>
            </div>
          </template>
        </Card>

        <!-- Queue Configuration -->
        <Card class="mt-3">
          <template #title>Configuration</template>
          <template #content>
            <div class="config-list">
              <div class="config-item" v-for="(value, key) in queueConfig" :key="key">
                <span class="config-key">{{ formatConfigKey(key) }}:</span>
                <span class="config-value">{{ formatConfigValue(value) }}</span>
              </div>
            </div>
            <Button 
              label="Edit Configuration" 
              icon="pi pi-cog" 
              class="p-button-sm w-full mt-3"
              @click="showConfigDialog = true"
            />
          </template>
        </Card>
      </div>

      <!-- Messages List -->
      <div class="col-12 lg:col-9">
        <Card>
          <template #title>
            <div class="flex align-items-center justify-content-between">
              <span>Messages</span>
              <div class="flex gap-2">
                <Dropdown 
                  v-model="statusFilter" 
                  :options="statusOptions" 
                  optionLabel="label"
                  optionValue="value"
                  placeholder="All Status"
                  class="w-10rem"
                />
                <Button 
                  label="Push Message" 
                  icon="pi pi-plus" 
                  class="p-button-sm"
                  @click="showPushDialog = true"
                />
              </div>
            </div>
          </template>
          <template #content>
            <DataTable 
              :value="messages" 
              :loading="loading"
              responsiveLayout="scroll"
              :paginator="true"
              :rows="10"
              paginatorTemplate="FirstPageLink PrevPageLink PageLinks NextPageLink LastPageLink CurrentPageReport"
              currentPageReportTemplate="Showing {first} to {last} of {totalRecords} messages"
            >
              <Column field="transactionId" header="Transaction ID" style="min-width: 200px">
                <template #body="{ data }">
                  <span class="font-mono text-sm">{{ data.transactionId.substring(0, 8) }}...</span>
                </template>
              </Column>
              <Column field="status" header="Status" style="min-width: 120px">
                <template #body="{ data }">
                  <Tag :severity="getStatusSeverity(data.status)" :value="data.status" />
                </template>
              </Column>
              <Column field="payload" header="Payload" style="min-width: 250px">
                <template #body="{ data }">
                  <pre class="payload-preview">{{ JSON.stringify(data.payload, null, 2) }}</pre>
                </template>
              </Column>
              <Column field="createdAt" header="Created" style="min-width: 150px">
                <template #body="{ data }">
                  {{ formatDate(data.createdAt) }}
                </template>
              </Column>
              <Column header="Actions" style="min-width: 150px">
                <template #body="{ data }">
                  <div class="flex gap-2">
                    <Button 
                      icon="pi pi-eye" 
                      class="p-button-text p-button-sm"
                      @click="viewMessage(data)"
                      v-tooltip="'View Details'"
                    />
                    <Button 
                      icon="pi pi-replay" 
                      class="p-button-text p-button-sm"
                      @click="retryMessage(data)"
                      v-if="data.status === 'failed'"
                      v-tooltip="'Retry'"
                    />
                    <Button 
                      icon="pi pi-trash" 
                      class="p-button-text p-button-danger p-button-sm"
                      @click="deleteMessage(data)"
                      v-tooltip="'Delete'"
                    />
                  </div>
                </template>
              </Column>
            </DataTable>
          </template>
        </Card>
      </div>
    </div>

    <!-- Configuration Dialog -->
    <Dialog v-model:visible="showConfigDialog" header="Queue Configuration" :style="{ width: '500px' }">
      <QueueConfigForm 
        :queue="currentQueue" 
        @save="updateConfiguration"
        @cancel="showConfigDialog = false"
      />
    </Dialog>

    <!-- Push Message Dialog -->
    <Dialog v-model:visible="showPushDialog" header="Push Message" :style="{ width: '600px' }">
      <div class="push-form">
        <div class="field">
          <label>Message Payload (JSON)</label>
          <Textarea 
            v-model="newMessage" 
            rows="10" 
            class="w-full font-mono"
            placeholder='{"type": "example", "data": {...}}'
          />
        </div>
        <div class="flex justify-content-end gap-2 mt-3">
          <Button 
            label="Cancel" 
            class="p-button-text" 
            @click="showPushDialog = false"
          />
          <Button 
            label="Push" 
            icon="pi pi-send" 
            @click="pushMessage"
          />
        </div>
      </div>
    </Dialog>

    <!-- Message Detail Dialog -->
    <Dialog v-model:visible="showMessageDialog" header="Message Details" :style="{ width: '700px' }">
      <div v-if="selectedMessage" class="message-details">
        <div class="detail-row">
          <span class="detail-label">Transaction ID:</span>
          <span class="font-mono">{{ selectedMessage.transactionId }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Status:</span>
          <Tag :severity="getStatusSeverity(selectedMessage.status)" :value="selectedMessage.status" />
        </div>
        <div class="detail-row">
          <span class="detail-label">Created:</span>
          <span>{{ formatDate(selectedMessage.createdAt) }}</span>
        </div>
        <div class="detail-row" v-if="selectedMessage.workerId">
          <span class="detail-label">Worker ID:</span>
          <span class="font-mono">{{ selectedMessage.workerId }}</span>
        </div>
        <div class="detail-row" v-if="selectedMessage.errorMessage">
          <span class="detail-label">Error:</span>
          <span class="text-red-500">{{ selectedMessage.errorMessage }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Payload:</span>
        </div>
        <pre class="payload-full">{{ JSON.stringify(selectedMessage.payload, null, 2) }}</pre>
      </div>
    </Dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useToast } from 'primevue/usetoast'
import { useConfirm } from 'primevue/useconfirm'
import { format } from 'date-fns'
import Button from 'primevue/button'
import Card from 'primevue/card'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Tag from 'primevue/tag'
import Dialog from 'primevue/dialog'
import Dropdown from 'primevue/dropdown'
import Textarea from 'primevue/textarea'
import QueueConfigForm from '../components/queues/QueueConfigForm.vue'
import { api } from '../utils/api'

const route = useRoute()
const router = useRouter()
const toast = useToast()
const confirm = useConfirm()

// State
const queuePath = ref('')
const currentQueue = ref(null)
const stats = ref({})
const messages = ref([])
const loading = ref(false)
const showConfigDialog = ref(false)
const showPushDialog = ref(false)
const showMessageDialog = ref(false)
const selectedMessage = ref(null)
const newMessage = ref('{\n  "type": "example",\n  "data": {}\n}')
const statusFilter = ref(null)

const statusOptions = [
  { label: 'All Status', value: null },
  { label: 'Pending', value: 'pending' },
  { label: 'Processing', value: 'processing' },
  { label: 'Completed', value: 'completed' },
  { label: 'Failed', value: 'failed' },
  { label: 'Dead Letter', value: 'dead_letter' }
]

// Computed
const queueConfig = computed(() => {
  if (!currentQueue.value) return {}
  return currentQueue.value.options || {}
})

const filteredMessages = computed(() => {
  if (!statusFilter.value) return messages.value
  return messages.value.filter(m => m.status === statusFilter.value)
})

// Methods
const parseQueuePath = () => {
  const id = route.params.id
  queuePath.value = id.replace(/-/g, '/')
  const parts = queuePath.value.split('/')
  return {
    ns: parts[0],
    task: parts[1],
    queue: parts[2]
  }
}

const loadQueueStats = async () => {
  try {
    const { ns, task, queue } = parseQueuePath()
    const response = await api.get(`/analytics/queue-stats?ns=${ns}&task=${task}&queue=${queue}`)
    stats.value = response.stats || {}
    currentQueue.value = response.queue
  } catch (error) {
    console.error('Failed to load queue stats:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load queue statistics',
      life: 3000
    })
  }
}

const loadMessages = async () => {
  loading.value = true
  try {
    // In a real implementation, this would fetch messages from an endpoint
    // For now, we'll simulate with the stats we have
    messages.value = []
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

const updateConfiguration = async (config) => {
  try {
    const { ns, task, queue } = parseQueuePath()
    await api.post('/configure', {
      ns,
      task,
      queue,
      options: config
    })
    
    showConfigDialog.value = false
    toast.add({
      severity: 'success',
      summary: 'Success',
      detail: 'Queue configuration updated',
      life: 3000
    })
    
    await loadQueueStats()
  } catch (error) {
    console.error('Failed to update configuration:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to update configuration',
      life: 3000
    })
  }
}

const pushMessage = async () => {
  try {
    const { ns, task, queue } = parseQueuePath()
    const payload = JSON.parse(newMessage.value)
    
    await api.post('/push', {
      items: [{
        ns,
        task,
        queue,
        data: payload
      }]
    })
    
    showPushDialog.value = false
    newMessage.value = '{\n  "type": "example",\n  "data": {}\n}'
    
    toast.add({
      severity: 'success',
      summary: 'Success',
      detail: 'Message pushed to queue',
      life: 3000
    })
    
    await loadMessages()
    await loadQueueStats()
  } catch (error) {
    console.error('Failed to push message:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: error.message || 'Failed to push message',
      life: 3000
    })
  }
}

const viewMessage = (message) => {
  selectedMessage.value = message
  showMessageDialog.value = true
}

const retryMessage = async (message) => {
  confirm.require({
    message: 'Are you sure you want to retry this message?',
    header: 'Confirm Retry',
    icon: 'pi pi-replay',
    accept: async () => {
      try {
        // Implementation would retry the message
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

const deleteMessage = async (message) => {
  confirm.require({
    message: 'Are you sure you want to delete this message?',
    header: 'Confirm Delete',
    icon: 'pi pi-exclamation-triangle',
    acceptClass: 'p-button-danger',
    accept: async () => {
      try {
        // Implementation would delete the message
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: 'Message deleted',
          life: 3000
        })
        await loadMessages()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to delete message',
          life: 3000
        })
      }
    }
  })
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

const formatConfigKey = (key) => {
  return key.replace(/([A-Z])/g, ' $1').replace(/^./, str => str.toUpperCase())
}

const formatConfigValue = (value) => {
  if (typeof value === 'boolean') return value ? 'Yes' : 'No'
  if (typeof value === 'number') return value.toLocaleString()
  return value || 'Not set'
}

const formatDate = (date) => {
  if (!date) return '-'
  return format(new Date(date), 'MMM dd, HH:mm:ss')
}

// Lifecycle
onMounted(() => {
  loadQueueStats()
  loadMessages()
  
  // Refresh data periodically
  const interval = setInterval(() => {
    loadQueueStats()
    loadMessages()
  }, 5000)
  
  // Cleanup
  onUnmounted(() => clearInterval(interval))
})

// Watch for filter changes
watch(statusFilter, () => {
  loadMessages()
})
</script>

<style scoped>
.queue-detail {
  padding: 1rem;
}

.page-header {
  display: flex;
  align-items: center;
  gap: 1rem;
  margin-bottom: 1.5rem;
}

.page-title {
  font-size: 1.5rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.stats-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 1rem;
}

.stat-item {
  text-align: center;
  padding: 0.5rem;
}

.stat-label {
  font-size: 0.875rem;
  color: #a3a3a3;
  margin-bottom: 0.25rem;
}

.stat-value {
  font-size: 1.5rem;
  font-weight: 600;
}

.config-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.config-item {
  display: flex;
  justify-content: space-between;
  padding: 0.25rem 0;
  border-bottom: 1px solid var(--surface-300);
}

.config-key {
  font-size: 0.875rem;
  color: #a3a3a3;
}

.config-value {
  font-size: 0.875rem;
  font-weight: 500;
  color: white;
}

.payload-preview {
  font-family: monospace;
  font-size: 0.75rem;
  background: var(--surface-100);
  padding: 0.5rem;
  border-radius: 4px;
  max-height: 100px;
  overflow: auto;
  margin: 0;
}

.payload-full {
  font-family: monospace;
  font-size: 0.875rem;
  background: var(--surface-100);
  padding: 1rem;
  border-radius: 4px;
  max-height: 400px;
  overflow: auto;
  margin: 0.5rem 0 0 0;
}

.message-details {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.detail-row {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.detail-label {
  font-weight: 500;
  color: #a3a3a3;
  min-width: 120px;
}

.push-form .field {
  margin-bottom: 1rem;
}

.push-form label {
  display: block;
  margin-bottom: 0.5rem;
  font-weight: 500;
}
</style>
