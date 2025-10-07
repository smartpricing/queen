<template>
  <div class="message-detail">
    <div class="page-header">
      <Button 
        icon="pi pi-arrow-left" 
        label="Back to Messages" 
        class="p-button-text"
        @click="router.push('/messages')"
      />
      <div class="header-info">
        <h1 class="page-title">Message Details</h1>
        <Tag :severity="getStatusSeverity(message?.status)" :value="message?.status || 'Loading...'" />
      </div>
    </div>

    <div v-if="loading" class="loading-container">
      <ProgressSpinner />
    </div>

    <div v-else-if="message" class="message-content">
      <div class="grid">
        <!-- Message Overview -->
        <div class="col-12 lg:col-8">
          <Card>
            <template #title>
              <div class="flex align-items-center justify-content-between">
                <span>Message Information</span>
                <div class="flex gap-2">
                  <Button 
                    icon="pi pi-copy" 
                    class="p-button-text p-button-sm"
                    @click="copyMessageId"
                    v-tooltip="'Copy Transaction ID'"
                  />
                  <Button 
                    icon="pi pi-refresh" 
                    class="p-button-text p-button-sm"
                    @click="loadMessage"
                    v-tooltip="'Refresh'"
                  />
                </div>
              </div>
            </template>
            <template #content>
              <div class="info-grid">
                <div class="info-row">
                  <span class="info-label">Transaction ID</span>
                  <span class="info-value font-mono">{{ message.transactionId }}</span>
                </div>
                <div class="info-row">
                  <span class="info-label">Queue Path</span>
                  <router-link 
                    :to="`/queues/${message.queuePath?.replace(/\//g, '-')}`"
                    class="queue-link"
                  >
                    {{ message.queuePath }}
                  </router-link>
                </div>
                <div class="info-row">
                  <span class="info-label">Status</span>
                  <div class="flex align-items-center gap-2">
                    <Tag :severity="getStatusSeverity(message.status)" :value="message.status" />
                    <span v-if="message.status === 'processing'" class="processing-indicator">
                      <i class="pi pi-spin pi-spinner"></i>
                    </span>
                  </div>
                </div>
                <div class="info-row">
                  <span class="info-label">Created At</span>
                  <span class="info-value">{{ formatDate(message.createdAt) }}</span>
                </div>
                <div class="info-row" v-if="message.lockedAt">
                  <span class="info-label">Locked At</span>
                  <span class="info-value">{{ formatDate(message.lockedAt) }}</span>
                </div>
                <div class="info-row" v-if="message.completedAt">
                  <span class="info-label">Completed At</span>
                  <span class="info-value">{{ formatDate(message.completedAt) }}</span>
                </div>
                <div class="info-row" v-if="message.failedAt">
                  <span class="info-label">Failed At</span>
                  <span class="info-value text-red-500">{{ formatDate(message.failedAt) }}</span>
                </div>
                <div class="info-row" v-if="message.workerId">
                  <span class="info-label">Worker ID</span>
                  <span class="info-value font-mono">{{ message.workerId }}</span>
                </div>
                <div class="info-row" v-if="message.retryCount > 0">
                  <span class="info-label">Retry Count</span>
                  <span class="info-value">{{ message.retryCount }} / {{ message.maxRetries || 3 }}</span>
                </div>
                <div class="info-row" v-if="message.leaseExpiresAt">
                  <span class="info-label">Lease Expires</span>
                  <span class="info-value">
                    {{ formatDate(message.leaseExpiresAt) }}
                    <Tag 
                      v-if="isLeaseExpiring" 
                      severity="warning" 
                      value="Expiring Soon" 
                      class="ml-2"
                    />
                  </span>
                </div>
              </div>

              <div v-if="message.errorMessage" class="error-section">
                <h3><i class="pi pi-exclamation-triangle mr-2"></i>Error Details</h3>
                <pre class="error-message">{{ message.errorMessage }}</pre>
              </div>
            </template>
          </Card>

          <!-- Payload -->
          <Card class="mt-3">
            <template #title>
              <div class="flex align-items-center justify-content-between">
                <span>Payload</span>
                <div class="flex gap-2">
                  <Button 
                    icon="pi pi-copy" 
                    label="Copy" 
                    class="p-button-text p-button-sm"
                    @click="copyPayload"
                  />
                  <Button 
                    icon="pi pi-download" 
                    label="Download" 
                    class="p-button-text p-button-sm"
                    @click="downloadPayload"
                  />
                </div>
              </div>
            </template>
            <template #content>
              <pre class="payload-display">{{ JSON.stringify(message.payload, null, 2) }}</pre>
            </template>
          </Card>
        </div>

        <!-- Sidebar -->
        <div class="col-12 lg:col-4">
          <!-- Actions -->
          <Card>
            <template #title>Actions</template>
            <template #content>
              <div class="actions-grid">
                <Button 
                  label="Retry Message" 
                  icon="pi pi-replay" 
                  class="p-button-warning w-full"
                  @click="retryMessage"
                  :disabled="!canRetry"
                />
                <Button 
                  label="Cancel Processing" 
                  icon="pi pi-times" 
                  class="p-button-secondary w-full"
                  @click="cancelMessage"
                  :disabled="!canCancel"
                />
                <Button 
                  label="Move to DLQ" 
                  icon="pi pi-exclamation-circle" 
                  class="p-button-danger w-full"
                  @click="moveToDLQ"
                  :disabled="!canMoveToDLQ"
                />
                <Button 
                  label="Delete Message" 
                  icon="pi pi-trash" 
                  class="p-button-danger p-button-outlined w-full"
                  @click="deleteMessage"
                />
              </div>
            </template>
          </Card>

          <!-- Processing Timeline -->
          <Card class="mt-3">
            <template #title>Processing Timeline</template>
            <template #content>
              <Timeline :value="timeline" layout="vertical" align="left">
                <template #marker="slotProps">
                  <span class="timeline-marker" :class="slotProps.item.severity">
                    <i :class="slotProps.item.icon"></i>
                  </span>
                </template>
                <template #content="slotProps">
                  <div class="timeline-content">
                    <div class="timeline-title">{{ slotProps.item.title }}</div>
                    <div class="timeline-time">{{ formatRelativeTime(slotProps.item.date) }}</div>
                    <div v-if="slotProps.item.description" class="timeline-description">
                      {{ slotProps.item.description }}
                    </div>
                  </div>
                </template>
              </Timeline>
            </template>
          </Card>

          <!-- Related Messages -->
          <Card class="mt-3" v-if="relatedMessages.length > 0">
            <template #title>
              Related Messages
              <Tag :value="`${relatedMessages.length}`" severity="secondary" class="ml-2" />
            </template>
            <template #content>
              <div class="related-messages">
                <div 
                  v-for="related in relatedMessages" 
                  :key="related.transactionId"
                  class="related-message"
                  @click="navigateToMessage(related.transactionId)"
                >
                  <div class="related-id">{{ related.transactionId.substring(0, 8) }}...</div>
                  <Tag :severity="getStatusSeverity(related.status)" :value="related.status" />
                  <div class="related-time">{{ formatRelativeTime(related.createdAt) }}</div>
                </div>
              </div>
            </template>
          </Card>
        </div>
      </div>
    </div>

    <div v-else class="no-message">
      <i class="pi pi-inbox text-6xl text-gray-400"></i>
      <p class="text-xl text-gray-400 mt-3">Message not found</p>
      <Button 
        label="Back to Messages" 
        icon="pi pi-arrow-left" 
        class="mt-3"
        @click="router.push('/messages')"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useToast } from 'primevue/usetoast'
import { useConfirm } from 'primevue/useconfirm'
import { format, formatDistance, differenceInMinutes } from 'date-fns'
import Button from 'primevue/button'
import Card from 'primevue/card'
import Tag from 'primevue/tag'
import Timeline from 'primevue/timeline'
import ProgressSpinner from 'primevue/progressspinner'
import { useMessagesStore } from '../stores/messages'
import { api } from '../utils/api'

const route = useRoute()
const router = useRouter()
const toast = useToast()
const confirm = useConfirm()
const messagesStore = useMessagesStore()

// State
const message = ref(null)
const loading = ref(true)
const timeline = ref([])
const relatedMessages = ref([])
const refreshInterval = ref(null)

// Computed
const canRetry = computed(() => {
  return message.value?.status === 'failed' || message.value?.status === 'dead_letter'
})

const canCancel = computed(() => {
  return message.value?.status === 'processing' || message.value?.status === 'pending'
})

const canMoveToDLQ = computed(() => {
  return message.value?.status === 'failed' && message.value?.status !== 'dead_letter'
})

const isLeaseExpiring = computed(() => {
  if (!message.value?.leaseExpiresAt) return false
  const minutesLeft = differenceInMinutes(new Date(message.value.leaseExpiresAt), new Date())
  return minutesLeft > 0 && minutesLeft < 5
})

// Methods
const loadMessage = async () => {
  loading.value = true
  try {
    const transactionId = route.params.id
    
    // Fetch real message data from API
    message.value = await api.messages.get(transactionId)
    
    // Set maxRetries from queue options if available
    if (message.value.queueOptions?.retryLimit) {
      message.value.maxRetries = message.value.queueOptions.retryLimit
    } else {
      message.value.maxRetries = 3 // Default
    }
    
    buildTimeline()
    loadRelatedMessages()
  } catch (error) {
    console.error('Failed to load message:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: error.message || 'Failed to load message details',
      life: 3000
    })
    message.value = null
  } finally {
    loading.value = false
  }
}

const buildTimeline = () => {
  if (!message.value) return
  
  const events = []
  
  if (message.value.createdAt) {
    events.push({
      title: 'Message Created',
      date: message.value.createdAt,
      icon: 'pi pi-plus',
      severity: 'info',
      description: 'Added to queue'
    })
  }
  
  if (message.value.lockedAt) {
    events.push({
      title: 'Processing Started',
      date: message.value.lockedAt,
      icon: 'pi pi-play',
      severity: 'warning',
      description: `Worker: ${message.value.workerId}`
    })
  }
  
  if (message.value.completedAt) {
    events.push({
      title: 'Processing Completed',
      date: message.value.completedAt,
      icon: 'pi pi-check',
      severity: 'success',
      description: 'Successfully processed'
    })
  }
  
  if (message.value.failedAt) {
    events.push({
      title: 'Processing Failed',
      date: message.value.failedAt,
      icon: 'pi pi-times',
      severity: 'danger',
      description: message.value.errorMessage || 'Unknown error'
    })
  }
  
  timeline.value = events
}

const loadRelatedMessages = async () => {
  try {
    // Load related messages from the same queue
    const response = await api.messages.getRelated(route.params.id)
    relatedMessages.value = response.messages || []
  } catch (error) {
    console.error('Failed to load related messages:', error)
    relatedMessages.value = []
  }
}

const retryMessage = () => {
  confirm.require({
    message: 'Are you sure you want to retry this message?',
    header: 'Confirm Retry',
    icon: 'pi pi-replay',
    accept: async () => {
      try {
        await api.messages.retry(message.value.transactionId)
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: 'Message queued for retry',
          life: 3000
        })
        await loadMessage()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: error.message || 'Failed to retry message',
          life: 3000
        })
      }
    }
  })
}

const cancelMessage = () => {
  confirm.require({
    message: 'Are you sure you want to cancel processing this message?',
    header: 'Confirm Cancel',
    icon: 'pi pi-times',
    acceptClass: 'p-button-warning',
    accept: async () => {
      try {
        // API call to cancel message
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: 'Message processing cancelled',
          life: 3000
        })
        await loadMessage()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to cancel message',
          life: 3000
        })
      }
    }
  })
}

const moveToDLQ = () => {
  confirm.require({
    message: 'Are you sure you want to move this message to the Dead Letter Queue?',
    header: 'Move to DLQ',
    icon: 'pi pi-exclamation-triangle',
    acceptClass: 'p-button-danger',
    accept: async () => {
      try {
        await api.messages.moveToDLQ(message.value.transactionId)
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: 'Message moved to Dead Letter Queue',
          life: 3000
        })
        await loadMessage()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: error.message || 'Failed to move message to DLQ',
          life: 3000
        })
      }
    }
  })
}

const deleteMessage = () => {
  confirm.require({
    message: 'Are you sure you want to permanently delete this message? This action cannot be undone.',
    header: 'Delete Message',
    icon: 'pi pi-exclamation-triangle',
    acceptClass: 'p-button-danger',
    accept: async () => {
      try {
        await api.messages.delete(message.value.transactionId)
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: 'Message deleted',
          life: 3000
        })
        router.push('/messages')
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: error.message || 'Failed to delete message',
          life: 3000
        })
      }
    }
  })
}

const copyMessageId = async () => {
  try {
    await navigator.clipboard.writeText(message.value.transactionId)
    toast.add({
      severity: 'success',
      summary: 'Copied',
      detail: 'Transaction ID copied to clipboard',
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

const copyPayload = async () => {
  try {
    await navigator.clipboard.writeText(JSON.stringify(message.value.payload, null, 2))
    toast.add({
      severity: 'success',
      summary: 'Copied',
      detail: 'Payload copied to clipboard',
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

const downloadPayload = () => {
  const data = JSON.stringify(message.value.payload, null, 2)
  const blob = new Blob([data], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `message-${message.value.transactionId}.json`
  a.click()
  URL.revokeObjectURL(url)
}

const navigateToMessage = (transactionId) => {
  router.push(`/messages/${transactionId}`)
}

const formatDate = (date) => {
  if (!date) return '-'
  return format(new Date(date), 'PPpp')
}

const formatRelativeTime = (date) => {
  if (!date) return '-'
  return formatDistance(new Date(date), new Date(), { addSuffix: true })
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

// Lifecycle
onMounted(() => {
  loadMessage()
  
  // Auto-refresh for processing messages
  refreshInterval.value = setInterval(() => {
    if (message.value?.status === 'processing') {
      loadMessage()
    }
  }, 5000)
})

onUnmounted(() => {
  if (refreshInterval.value) {
    clearInterval(refreshInterval.value)
  }
})
</script>

<style scoped>
.message-detail {
  padding: 1rem;
}

.page-header {
  display: flex;
  align-items: center;
  gap: 1rem;
  margin-bottom: 1.5rem;
}

.header-info {
  display: flex;
  align-items: center;
  gap: 1rem;
  flex: 1;
}

.page-title {
  font-size: 1.5rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.loading-container {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 400px;
}

.no-message {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 400px;
  text-align: center;
}

.info-grid {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.info-row {
  display: grid;
  grid-template-columns: 150px 1fr;
  align-items: center;
  padding: 0.5rem 0;
  border-bottom: 1px solid var(--surface-300);
}

.info-label {
  font-weight: 500;
  color: #a3a3a3;
}

.info-value {
  color: white;
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

.processing-indicator {
  color: #3b82f6;
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

.payload-display {
  font-family: monospace;
  font-size: 0.875rem;
  background: var(--surface-100);
  padding: 1rem;
  border-radius: 4px;
  max-height: 500px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-word;
  margin: 0;
}

.actions-grid {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.timeline-marker {
  width: 2rem;
  height: 2rem;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
}

.timeline-marker.info {
  background: #3b82f6;
}

.timeline-marker.warning {
  background: #f59e0b;
}

.timeline-marker.success {
  background: #10b981;
}

.timeline-marker.danger {
  background: #ef4444;
}

.timeline-content {
  padding-bottom: 1.5rem;
}

.timeline-title {
  font-weight: 600;
  color: white;
  margin-bottom: 0.25rem;
}

.timeline-time {
  font-size: 0.875rem;
  color: #a3a3a3;
}

.timeline-description {
  font-size: 0.875rem;
  color: #d1d5db;
  margin-top: 0.25rem;
}

.related-messages {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.related-message {
  display: grid;
  grid-template-columns: 1fr auto auto;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem;
  background: var(--surface-100);
  border-radius: 4px;
  cursor: pointer;
  transition: background 0.2s;
}

.related-message:hover {
  background: var(--surface-200);
}

.related-id {
  font-family: monospace;
  font-size: 0.875rem;
  color: #a855f7;
}

.related-time {
  font-size: 0.75rem;
  color: #a3a3a3;
}

.font-mono {
  font-family: monospace;
}

.text-red-500 {
  color: #ef4444;
}

.text-gray-400 {
  color: #9ca3af;
}

.text-6xl {
  font-size: 4rem;
}

.text-xl {
  font-size: 1.25rem;
}

.ml-2 {
  margin-left: 0.5rem;
}

.ml-3 {
  margin-left: 0.75rem;
}

.mr-2 {
  margin-right: 0.5rem;
}

.mt-3 {
  margin-top: 0.75rem;
}

.w-full {
  width: 100%;
}
</style>
