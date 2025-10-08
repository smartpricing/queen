<template>
  <div class="queues-view">
    <div class="view-header">
      <h1>Queue Management</h1>
      <div class="header-actions">
        <Button label="Refresh" icon="pi pi-refresh" @click="fetchQueues" :loading="loading" />
      </div>
    </div>

    <Card>
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
      >
        <template #header>
          <div class="table-header">
            <span class="p-input-icon-left">
              <i class="pi pi-search" />
              <InputText v-model="filters.global" placeholder="Search queues..." />
            </span>
          </div>
        </template>

        <Column field="name" header="Queue Name" :sortable="true">
          <template #body="{ data }">
            <router-link :to="`/queues/${data.name}`" class="queue-link">
              {{ data.name }}
            </router-link>
          </template>
        </Column>
        
        <Column field="namespace" header="Namespace" :sortable="true">
          <template #body="{ data }">
            <Tag v-if="data.namespace" :value="data.namespace" severity="secondary" />
            <span v-else class="text-muted">-</span>
          </template>
        </Column>
        
        <Column field="task" header="Task" :sortable="true">
          <template #body="{ data }">
            <Tag v-if="data.task" :value="data.task" severity="secondary" />
            <span v-else class="text-muted">-</span>
          </template>
        </Column>
        
        <Column field="partitions" header="Partitions" :sortable="true">
          <template #body="{ data }">
            <Badge :value="data.partitions" />
          </template>
        </Column>
        
        <Column header="Messages">
          <template #body="{ data }">
            <div class="message-stats">
              <Tag :value="`${data.messages?.pending || 0} pending`" severity="warn" />
              <Tag :value="`${data.messages?.processing || 0} processing`" severity="info" />
            </div>
          </template>
        </Column>
        
        <Column field="createdAt" header="Created" :sortable="true">
          <template #body="{ data }">
            {{ formatDate(data.createdAt, 'short') }}
          </template>
        </Column>
        
        <Column header="Actions" style="width: 100px">
          <template #body="{ data }">
            <Button 
              icon="pi pi-eye" 
              class="p-button-text p-button-sm"
              @click="$router.push(`/queues/${data.name}`)"
              v-tooltip="'View Details'"
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
import { useToast } from 'primevue/usetoast'
import Card from 'primevue/card'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import InputText from 'primevue/inputtext'
import Badge from 'primevue/badge'
import Tag from 'primevue/tag'

import api from '../services/api.js'
import { formatDate } from '../utils/helpers.js'

const toast = useToast()
const loading = ref(false)
const queues = ref([]) // Initialize with empty array
const filters = ref({ global: '' })

const fetchQueues = async () => {
  try {
    loading.value = true
    const data = await api.getQueues()
    queues.value = data.queues || []
  } catch (error) {
    console.error('Failed to fetch queues:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load queues',
      life: 3000
    })
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  fetchQueues()
})
</script>

<style scoped>
.queues-view {
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

.table-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.queue-link {
  color: var(--primary-color);
  text-decoration: none;
  font-weight: 500;
}

.queue-link:hover {
  text-decoration: underline;
}

.message-stats {
  display: flex;
  gap: 0.5rem;
}

.text-muted {
  color: var(--gray-400);
}
</style>
