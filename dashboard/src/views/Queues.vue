<template>
  <div class="queues-view">
    <Card class="queues-card">
      <template #content>
        <DataTable 
        :value="queues" 
        :loading="loading"
        :paginator="true"
        :rows="10"
        :rowsPerPageOptions="[10, 20, 50]"
        responsiveLayout="scroll"
        filterDisplay="row"
        :globalFilterFields="['name', 'namespace', 'task']"
        class="dark-table-v3"
      >
        <template #header>
          <div class="table-header">
            <h2>Queue Management</h2>
            <div class="table-actions">
              <span class="p-input-icon-left">
                <i class="pi pi-search" />
                <InputText v-model="filters.global" placeholder="Search queues..." class="search-input" />
              </span>
              <Button icon="pi pi-refresh" @click="fetchQueues" :loading="loading" class="p-button-text" />
            </div>
          </div>
        </template>

        <Column field="name" header="Queue Name" :sortable="true">
          <template #body="{ data }">
            <div class="queue-name-cell">
              <div class="queue-icon">Q</div>
              <router-link :to="`/queues/${data.name}`" class="queue-link">
                {{ data.name }}
              </router-link>
            </div>
          </template>
        </Column>
        
        <Column field="namespace" header="Namespace" :sortable="true">
          <template #body="{ data }">
            <Tag v-if="data.namespace" :value="data.namespace" class="namespace-tag" />
            <span v-else class="text-muted">-</span>
          </template>
        </Column>
        
        <Column field="task" header="Task" :sortable="true">
          <template #body="{ data }">
            <Tag v-if="data.task" :value="data.task" class="task-tag" />
            <span v-else class="text-muted">-</span>
          </template>
        </Column>
        
        <Column header="Status">
          <template #body="{ data }">
            <div class="status-badges">
              <span class="status-pending">{{ data.stats?.pending || 0 }} pending</span>
              <span class="status-processing">{{ data.stats?.processing || 0 }} processing</span>
            </div>
          </template>
        </Column>
        
        <Column field="stats.completed" header="Completed" :sortable="true">
          <template #body="{ data }">
            <span class="status-completed">{{ data.stats?.completed || 0 }}</span>
          </template>
        </Column>
        
        <Column field="stats.failed" header="Failed" :sortable="true">
          <template #body="{ data }">
            <span class="status-failed">{{ data.stats?.failed || 0 }}</span>
          </template>
        </Column>
        
        <Column header="Actions" :exportable="false" style="min-width: 8rem">
          <template #body="{ data }">
            <Button 
              icon="pi pi-cog" 
              class="p-button-text p-button-sm action-btn"
              @click="configureQueue(data)"
              v-tooltip="'Configure'"
            />
            <Button 
              icon="pi pi-trash" 
              class="p-button-text p-button-sm p-button-danger action-btn"
              @click="deleteQueue(data)"
              v-tooltip="'Delete'"
            />
          </template>
        </Column>
      </DataTable>
      </template>
    </Card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useToast } from 'primevue/usetoast'
import { useConfirm } from 'primevue/useconfirm'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import InputText from 'primevue/inputtext'
import Button from 'primevue/button'
import Tag from 'primevue/tag'
import Card from 'primevue/card'
import api from '../services/api.js'

const router = useRouter()
const toast = useToast()
const confirm = useConfirm()

const loading = ref(false)
const queues = ref([])
const filters = ref({
  global: ''
})

const fetchQueues = async () => {
  try {
    loading.value = true
    const data = await api.getQueues()
    
    if (data?.queues && Array.isArray(data.queues)) {
      // Process the actual API format: {queues: [{name, namespace, task, messages: {total, pending, processing}}]}
      queues.value = data.queues.map(queue => ({
        id: queue.id,
        name: queue.name,
        namespace: queue.namespace,
        task: queue.task,
        createdAt: queue.createdAt,
        partitions: queue.partitions,
        stats: {
          pending: queue.messages?.pending || 0,
          processing: queue.messages?.processing || 0,
          completed: (queue.messages?.total || 0) - (queue.messages?.pending || 0) - (queue.messages?.processing || 0),
          failed: queue.messages?.failed || 0,
          total: queue.messages?.total || 0
        }
      }))
    } else {
      // Fallback to empty array
      queues.value = []
    }
  } catch (error) {
    console.error('Failed to fetch queues:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load queues',
      life: 3000
    })
    queues.value = []
  } finally {
    loading.value = false
  }
}

const configureQueue = (queue) => {
  router.push(`/queues/${queue.name}/configure`)
}

const deleteQueue = (queue) => {
  confirm.require({
    message: `Are you sure you want to delete queue "${queue.name}"?`,
    header: 'Delete Queue',
    icon: 'pi pi-exclamation-triangle',
    acceptClass: 'p-button-danger',
    accept: async () => {
      try {
        await api.deleteQueue(queue.name)
        toast.add({
          severity: 'success',
          summary: 'Success',
          detail: `Queue "${queue.name}" deleted`,
          life: 3000
        })
        fetchQueues()
      } catch (error) {
        toast.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to delete queue',
          life: 3000
        })
      }
    }
  })
}

onMounted(() => {
  fetchQueues()
})
</script>

<style scoped>
.queues-view {
  padding: 0;
}

.queues-card {
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

.search-input {
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  color: var(--surface-600);
}

.queue-name-cell {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.queue-icon {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: linear-gradient(135deg, #ec4899 0%, #db2777 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  font-weight: 600;
  font-size: 0.875rem;
}

.queue-link {
  color: var(--primary-500);
  text-decoration: none;
  font-weight: 500;
}

.queue-link:hover {
  text-decoration: underline;
}

.namespace-tag,
.task-tag {
  background: rgba(236, 72, 153, 0.15);
  color: var(--primary-500);
  border: 1px solid rgba(236, 72, 153, 0.3);
}

.status-badges {
  display: flex;
  gap: 0.5rem;
}

.text-muted {
  color: var(--surface-400);
}

.action-btn {
  color: var(--surface-500) !important;
}

.action-btn:hover {
  background: rgba(236, 72, 153, 0.1) !important;
  color: var(--primary-500) !important;
}

.action-btn.p-button-danger:hover {
  background: rgba(239, 68, 68, 0.1) !important;
  color: var(--danger-color) !important;
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