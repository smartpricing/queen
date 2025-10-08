<template>
  <div class="queue-detail">
    <div class="view-header">
      <div>
        <Button 
          icon="pi pi-arrow-left" 
          class="p-button-text"
          @click="$router.push('/queues')"
        />
        <h1 style="display: inline; margin-left: 1rem;">{{ queueName }}</h1>
      </div>
      <Button label="Refresh" icon="pi pi-refresh" @click="fetchQueueDetail" :loading="loading" />
    </div>

    <div v-if="queueData" class="queue-content">
      <Card>
        <template #title>Queue Information</template>
        <template #content>
          <div class="info-grid">
          <div class="info-item">
            <span class="info-label">Namespace:</span>
            <Tag v-if="queueData.namespace" :value="queueData.namespace" />
            <span v-else>-</span>
          </div>
          <div class="info-item">
            <span class="info-label">Task:</span>
            <Tag v-if="queueData.task" :value="queueData.task" />
            <span v-else>-</span>
          </div>
          <div class="info-item">
            <span class="info-label">Created:</span>
            <span>{{ formatDate(queueData.createdAt) }}</span>
          </div>
          <div class="info-item">
            <span class="info-label">Total Messages:</span>
            <span>{{ queueData.totals?.total || 0 }}</span>
          </div>
        </div>
        </template>
      </Card>

      <Card style="margin-top: 1.5rem;">
        <template #title>Partitions</template>
        <template #content>
          <DataTable :value="queueData.partitions" :loading="loading">
          <Column field="name" header="Partition Name" />
          <Column field="priority" header="Priority" />
          <Column header="Stats">
            <template #body="{ data }">
              <div class="partition-stats">
                <Tag :value="`${data.stats?.pending || 0} pending`" severity="warn" />
                <Tag :value="`${data.stats?.processing || 0} processing`" severity="info" />
                <Tag :value="`${data.stats?.completed || 0} completed`" severity="success" />
              </div>
            </template>
          </Column>
        </DataTable>
        </template>
      </Card>
    </div>

    <div v-else-if="loading" class="loading-container">
      <ProgressSpinner />
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue'
import { useRoute } from 'vue-router'
import { useToast } from 'primevue/usetoast'
import Card from 'primevue/card'
import Button from 'primevue/button'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Tag from 'primevue/tag'
import ProgressSpinner from 'primevue/progressspinner'

import api from '../services/api.js'
import { formatDate } from '../utils/helpers.js'

const route = useRoute()
const toast = useToast()
const loading = ref(false)
const queueData = ref(null)

const queueName = computed(() => route.params.queueName)

const fetchQueueDetail = async () => {
  try {
    loading.value = true
    const data = await api.getQueueDetail(queueName.value)
    queueData.value = data
  } catch (error) {
    console.error('Failed to fetch queue detail:', error)
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

onMounted(() => {
  fetchQueueDetail()
})
</script>

<style scoped>
.queue-detail {
  max-width: 1200px;
  margin: 0 auto;
}

.view-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 2rem;
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
  font-size: 0.875rem;
  color: var(--gray-600);
  font-weight: 500;
}

.partition-stats {
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.loading-container {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 400px;
}
</style>
