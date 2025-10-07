<template>
  <div class="queues-view">
    <div class="view-header">
      <h2>Queue Management</h2>
      <div class="header-actions">
        <InputText 
          v-model="filters.search" 
          placeholder="Search queues..." 
          class="search-input"
        >
          <template #left>
            <i class="pi pi-search"></i>
          </template>
        </InputText>
        
        <Button 
          label="Configure Queue" 
          icon="pi pi-plus" 
          @click="showConfigDialog = true"
        />
      </div>
    </div>

    <DataTable 
      :value="filteredQueues" 
      :loading="loading"
      :paginator="true"
      :rows="10"
      :rowsPerPageOptions="[10, 25, 50]"
      responsiveLayout="scroll"
      :globalFilterFields="['queue']"
      sortField="stats.pending"
      :sortOrder="-1"
      class="queues-table"
    >
      <template #empty>
        <div class="empty-state">
          <i class="pi pi-inbox text-5xl"></i>
          <h3>No queues found</h3>
          <p>Create your first queue to get started</p>
        </div>
      </template>

      <Column field="queue" header="Queue" :sortable="true">
        <template #body="{ data }">
          <div class="queue-name">
            <router-link :to="`/queues/${encodeURIComponent(data.queue)}`">
              {{ data.queue }}
            </router-link>
          </div>
        </template>
      </Column>

      <Column field="priority" header="Priority" :sortable="true">
        <template #body="{ data }">
          <Tag :severity="getPrioritySeverity(data.priority)">
            {{ data.priority || 0 }}
          </Tag>
        </template>
      </Column>

      <Column field="stats.pending" header="Pending" :sortable="true">
        <template #body="{ data }">
          <div class="stat-cell">
            <Badge :value="data.stats?.pending || 0" severity="info" />
          </div>
        </template>
      </Column>

      <Column field="stats.processing" header="Processing" :sortable="true">
        <template #body="{ data }">
          <div class="stat-cell">
            <Badge :value="data.stats?.processing || 0" severity="warning" />
          </div>
        </template>
      </Column>

      <Column field="stats.completed" header="Completed" :sortable="true">
        <template #body="{ data }">
          <div class="stat-cell">
            <Badge :value="data.stats?.completed || 0" severity="success" />
          </div>
        </template>
      </Column>

      <Column field="stats.failed" header="Failed" :sortable="true">
        <template #body="{ data }">
          <div class="stat-cell">
            <Badge :value="data.stats?.failed || 0" severity="danger" />
          </div>
        </template>
      </Column>

      <Column header="Actions" :exportable="false" style="width: 150px">
        <template #body="{ data }">
          <div class="action-buttons">
            <Button 
              icon="pi pi-eye" 
              class="p-button-rounded p-button-text p-button-sm"
              v-tooltip="'View Details'"
              @click="viewQueueDetails(data)"
            />
            <Button 
              icon="pi pi-cog" 
              class="p-button-rounded p-button-text p-button-sm"
              v-tooltip="'Configure'"
              @click="configureQueue(data)"
            />
            <Button 
              icon="pi pi-trash" 
              class="p-button-rounded p-button-text p-button-danger p-button-sm"
              v-tooltip="'Clear Queue'"
              @click="clearQueue(data)"
            />
          </div>
        </template>
      </Column>
    </DataTable>

    <!-- Configure Dialog -->
    <Dialog 
      v-model:visible="showConfigDialog" 
      header="Configure Queue" 
      :modal="true"
      :style="{ width: '500px' }"
    >
      <QueueConfigForm 
        :queue="selectedQueue" 
        @save="onQueueConfigured"
        @cancel="showConfigDialog = false"
      />
    </Dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useConfirm } from 'primevue/useconfirm'
import { useToast } from 'primevue/usetoast'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import InputText from 'primevue/inputtext'
import Badge from 'primevue/badge'
import Tag from 'primevue/tag'
import Dialog from 'primevue/dialog'

import QueueConfigForm from '../components/queues/QueueConfigForm.vue'
import { useQueuesStore } from '../stores/queues'

const router = useRouter()
const confirm = useConfirm()
const toast = useToast()
const queuesStore = useQueuesStore()

const loading = computed(() => queuesStore.loading)
const queues = computed(() => queuesStore.queues)

const filters = ref({
  search: ''
})

const showConfigDialog = ref(false)
const selectedQueue = ref(null)

const filteredQueues = computed(() => {
  if (!filters.value.search) return queues.value
  
  const search = filters.value.search.toLowerCase()
  return queues.value.filter(q => 
    q.queue.toLowerCase().includes(search)
  )
})

function getPrioritySeverity(priority) {
  if (priority >= 10) return 'danger'
  if (priority >= 5) return 'warning'
  if (priority > 0) return 'info'
  return 'secondary'
}

function viewQueueDetails(queue) {
  router.push(`/queues/${encodeURIComponent(queue.queue)}`)
}

function configureQueue(queue) {
  selectedQueue.value = queue
  showConfigDialog.value = true
}

function clearQueue(queue) {
  confirm.require({
    message: `Are you sure you want to clear all messages in ${queue.queue}?`,
    header: 'Confirm Clear Queue',
    icon: 'pi pi-exclamation-triangle',
    acceptClass: 'p-button-danger',
    accept: async () => {
      try {
        const [ns, task, queueName] = queue.queue.split('/')
        const result = await api.queues.clear(ns, task, queueName)
        toast.add({
          severity: 'success',
          summary: 'Queue Cleared',
          detail: `Cleared ${result.count || 0} messages from ${queue.queue}`,
          life: 3000
        })
        // Refresh queue list to update counts
        await queuesStore.fetchQueues()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: error.message || 'Failed to clear queue',
          life: 3000
        })
      }
    }
  })
}

async function onQueueConfigured() {
  showConfigDialog.value = false
  selectedQueue.value = null
  await queuesStore.fetchQueues()
  
  toast.add({
    severity: 'success',
    summary: 'Queue Configured',
    detail: 'Queue configuration has been updated',
    life: 3000
  })
}

onMounted(() => {
  queuesStore.fetchQueues()
})
</script>

<style scoped>
.queues-view {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.view-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.view-header h2 {
  margin: 0;
  color: white;
  font-size: 1.75rem;
}

.header-actions {
  display: flex;
  gap: 1rem;
  align-items: center;
}

.search-input {
  width: 300px;
}

.queues-table {
  background: var(--surface-100);
  border: 1px solid var(--surface-300);
  border-radius: 8px;
  overflow: hidden;
}

.queue-name a {
  color: #a855f7;
  text-decoration: none;
  font-weight: 500;
}

.queue-name a:hover {
  text-decoration: underline;
}

.stat-cell {
  display: flex;
  align-items: center;
}

.action-buttons {
  display: flex;
  gap: 0.25rem;
}

.empty-state {
  padding: 3rem;
  text-align: center;
  color: #737373;
}

.empty-state h3 {
  margin: 1rem 0 0.5rem;
  color: #a3a3a3;
}
</style>
