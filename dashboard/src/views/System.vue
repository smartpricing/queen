<template>
  <div class="system-view">
    <div class="view-header">
      <h1>System Overview</h1>
      <Button label="Refresh" icon="pi pi-refresh" @click="fetchSystemInfo" :loading="loading" />
    </div>

    <div class="system-grid">
      <Card>
        <template #title>
          <div class="card-header-custom">
            <span>Health Status</span>
            <Tag :value="healthStatus" :severity="healthSeverity" />
          </div>
        </template>
        <template #content>
          <div class="health-info">
          <div class="health-item">
            <i class="pi pi-server"></i>
            <span>Uptime: {{ uptime }}</span>
          </div>
          <div class="health-item">
            <i class="pi pi-database"></i>
            <span>Connections: {{ connections }}</span>
          </div>
          <div class="health-item">
            <i class="pi pi-bolt"></i>
            <span>Request Rate: {{ requestRate }}/s</span>
          </div>
        </div>
        </template>
      </Card>

      <Card>
        <template #title>Memory Usage</template>
        <template #content>
          <div class="memory-stats">
          <div class="memory-item">
            <span class="memory-label">Heap Used</span>
            <ProgressBar :value="memoryPercentage" :showValue="false" />
            <span class="memory-value">{{ formatBytes(memory.heapUsed) }} / {{ formatBytes(memory.heapTotal) }}</span>
          </div>
          <div class="memory-item">
            <span class="memory-label">RSS</span>
            <span class="memory-value">{{ formatBytes(memory.rss) }}</span>
          </div>
        </div>
        </template>
      </Card>

      <Card>
        <template #title>Database Pool</template>
        <template #content>
          <div class="pool-stats">
          <div class="pool-item">
            <span class="pool-label">Total Connections</span>
            <span class="pool-value">{{ pool.total }}</span>
          </div>
          <div class="pool-item">
            <span class="pool-label">Idle</span>
            <span class="pool-value">{{ pool.idle }}</span>
          </div>
          <div class="pool-item">
            <span class="pool-label">Waiting</span>
            <span class="pool-value">{{ pool.waiting }}</span>
          </div>
        </div>
        </template>
      </Card>

      <Card>
        <template #title>System Summary</template>
        <template #content>
          <div class="summary-stats">
          <div class="summary-item">
            <span class="summary-label">Total Queues</span>
            <span class="summary-value">{{ summary.queues }}</span>
          </div>
          <div class="summary-item">
            <span class="summary-label">Total Partitions</span>
            <span class="summary-value">{{ summary.partitions }}</span>
          </div>
          <div class="summary-item">
            <span class="summary-label">Total Messages</span>
            <span class="summary-value">{{ formatNumber(summary.messages) }}</span>
          </div>
        </div>
        </template>
      </Card>
    </div>

    <Card style="margin-top: 1.5rem;">
      <template #title>Configuration</template>
      <template #content>
        <div class="config-info">
        <div class="config-item">
          <span class="config-label">API Version:</span>
          <span>v2.0.0</span>
        </div>
        <div class="config-item">
          <span class="config-label">Node Version:</span>
          <span>{{ nodeVersion }}</span>
        </div>
        <div class="config-item">
          <span class="config-label">Database:</span>
          <span>PostgreSQL</span>
        </div>
        <div class="config-item">
          <span class="config-label">WebSocket:</span>
          <Tag value="Enabled" severity="success" />
        </div>
      </div>
      </template>
    </Card>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue'
import { useToast } from 'primevue/usetoast'
import Card from 'primevue/card'
import Button from 'primevue/button'
import Tag from 'primevue/tag'
import ProgressBar from 'primevue/progressbar'

import api from '../services/api.js'
import { formatBytes, formatNumber } from '../utils/helpers.js'

const toast = useToast()
const loading = ref(false)

const healthStatus = ref('Healthy')
const healthSeverity = ref('success')
const uptime = ref('--')
const connections = ref(0)
const requestRate = ref('0')

const memory = ref({
  rss: 0,
  heapTotal: 0,
  heapUsed: 0
})

const pool = ref({
  total: 0,
  idle: 0,
  waiting: 0
})

const summary = ref({
  queues: 0,
  partitions: 0,
  messages: 0
})

const nodeVersion = ref('--')

const memoryPercentage = computed(() => {
  if (!memory.value.heapTotal) return 0
  return Math.round((memory.value.heapUsed / memory.value.heapTotal) * 100)
})

const fetchSystemInfo = async () => {
  try {
    loading.value = true
    
    const [health, metrics, overview] = await Promise.all([
      api.getHealth(),
      api.getMetrics(),
      api.getSystemOverview()
    ])

    // Process health data
    if (health) {
      healthStatus.value = health.status === 'healthy' ? 'Healthy' : 'Unhealthy'
      healthSeverity.value = health.status === 'healthy' ? 'success' : 'danger'
      connections.value = health.connections || 0
      
      // Parse uptime
      if (health.uptime) {
        const seconds = parseInt(health.uptime)
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor((seconds % 3600) / 60)
        
        if (hours > 24) {
          const days = Math.floor(hours / 24)
          uptime.value = `${days}d ${hours % 24}h ${minutes}m`
        } else {
          uptime.value = `${hours}h ${minutes}m`
        }
      }
      
      requestRate.value = health.stats?.requestsPerSecond || '0'
    }

    // Process metrics
    if (metrics) {
      memory.value = metrics.memory || {}
      pool.value = metrics.database || {}
      nodeVersion.value = 'v22.0.0' // Static version since process is not available in browser
    }

    // Process overview
    if (overview) {
      summary.value = {
        queues: overview.queues || 0,
        partitions: overview.partitions || 0,
        messages: overview.messages?.total || 0
      }
    }

  } catch (error) {
    console.error('Failed to fetch system info:', error)
    toast.add({
      severity: 'error',
      summary: 'Error',
      detail: 'Failed to load system information',
      life: 3000
    })
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  fetchSystemInfo()
  
  // Auto-refresh every 10 seconds
  setInterval(() => {
    fetchSystemInfo()
  }, 10000)
})
</script>

<style scoped>
.system-view {
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

.system-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  gap: 1.5rem;
}

.card-header-custom {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.health-info,
.memory-stats,
.pool-stats,
.summary-stats,
.config-info {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.health-item,
.memory-item,
.pool-item,
.summary-item,
.config-item {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.health-item i {
  font-size: 1.25rem;
  color: var(--primary-color);
}

.memory-label,
.pool-label,
.summary-label,
.config-label {
  font-size: 0.875rem;
  color: var(--gray-600);
  font-weight: 500;
  min-width: 120px;
}

.memory-value,
.pool-value,
.summary-value {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--gray-800);
}

.memory-item {
  flex-direction: column;
  align-items: flex-start;
}

.memory-value {
  font-size: 0.875rem;
  margin-top: 0.25rem;
}

.config-info {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 1rem;
}

@media (max-width: 768px) {
  .system-grid {
    grid-template-columns: 1fr;
  }
}
</style>
