<template>
  <header class="app-header">
    <div class="header-left">
      <h1 class="page-title">{{ pageTitle }}</h1>
      <Breadcrumb :model="breadcrumbs" class="breadcrumb" />
    </div>
    
    <div class="header-right">
      <div class="stats-mini">
        <div class="stat-item">
          <i class="pi pi-envelope text-blue-400"></i>
          <span>{{ stats.pending }}</span>
          <small>Pending</small>
        </div>
        <div class="stat-item">
          <i class="pi pi-spin pi-spinner text-yellow-400"></i>
          <span>{{ stats.processing }}</span>
          <small>Processing</small>
        </div>
        <div class="stat-item">
          <i class="pi pi-check-circle text-green-400"></i>
          <span>{{ stats.completed }}</span>
          <small>Completed</small>
        </div>
      </div>
      
      <Button 
        icon="pi pi-refresh" 
        class="p-button-rounded p-button-text"
        v-tooltip="'Refresh'"
        @click="refresh"
      />
      
      <Button 
        icon="pi pi-bell" 
        class="p-button-rounded p-button-text"
        badge="2"
        badgeClass="p-badge-danger"
        v-tooltip="'Notifications'"
      />
    </div>
  </header>
</template>

<script setup>
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import Button from 'primevue/button'
import Breadcrumb from 'primevue/breadcrumb'
import { useQueuesStore } from '../../stores/queues'

const route = useRoute()
const queuesStore = useQueuesStore()

const pageTitle = computed(() => route.meta?.title || 'Dashboard')

const breadcrumbs = computed(() => {
  const items = [{ label: 'Home', to: '/' }]
  if (route.name !== 'Dashboard') {
    items.push({ label: pageTitle.value })
  }
  return items
})

const stats = computed(() => queuesStore.globalStats)

const refresh = () => {
  queuesStore.fetchQueues()
}
</script>

<style scoped>
.app-header {
  background: var(--surface-100);
  border-bottom: 1px solid var(--surface-300);
  padding: 1rem 1.5rem;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.page-title {
  font-size: 1.5rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.breadcrumb :deep(.p-breadcrumb) {
  background: transparent;
  border: none;
  padding: 0;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 1.5rem;
}

.stats-mini {
  display: flex;
  gap: 1.5rem;
}

.stat-item {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  color: #a3a3a3;
}

.stat-item span {
  color: white;
  font-weight: 600;
}

.stat-item small {
  font-size: 0.75rem;
}

@media (max-width: 768px) {
  .stats-mini {
    display: none;
  }
}
</style>
