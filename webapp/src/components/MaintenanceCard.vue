<template>
  <div class="chart-card compact-card">
    <div class="chart-header-compact">
      <h3 class="chart-title-compact">Maintenance Mode</h3>
      <span 
        v-if="maintenanceMode" 
        class="inline-flex items-center px-1.5 py-0.5 rounded text-xs font-medium bg-yellow-100 text-yellow-800 dark:bg-yellow-900/30 dark:text-yellow-400"
      >
        ON
      </span>
      <span 
        v-else
        class="inline-flex items-center px-1.5 py-0.5 rounded text-xs font-medium bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400"
      >
        ACTIVE
      </span>
    </div>
    
    <div class="chart-body-compact">
      <LoadingSpinner v-if="loading" />
      
      <div v-else-if="error" class="text-sm text-red-600 dark:text-red-400">
        {{ error }}
      </div>
      
      <div v-else class="space-y-2.5">
        <!-- Buffer Status -->
        <div class="grid grid-cols-2 gap-2">
          <div class="stat-box-compact">
            <div class="stat-label-compact">Buffered</div>
            <div class="stat-value-compact" :class="bufferedMessages > 0 ? 'text-yellow-600 dark:text-yellow-400' : 'text-gray-600 dark:text-gray-300'">
              {{ formatNumber(bufferedMessages) }}
            </div>
          </div>
          
          <div class="stat-box-compact">
            <div class="stat-label-compact">Database</div>
            <div class="stat-value-compact" :class="bufferHealthy ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'">
              {{ bufferHealthy ? 'Healthy' : 'Down' }}
            </div>
          </div>
        </div>
        
        <!-- Failed Files (if any) -->
        <div v-if="failedFiles.count > 0" class="border-t border-gray-200 dark:border-gray-700 pt-2">
          <div class="flex items-center justify-between mb-1.5">
            <span class="text-xs font-medium text-gray-600 dark:text-gray-400">Failed Files</span>
            <span class="px-1.5 py-0.5 rounded text-xs font-medium bg-red-100 text-red-800 dark:bg-red-900/30 dark:text-red-400">
              {{ failedFiles.count }}
            </span>
          </div>
          
          <div class="grid grid-cols-3 gap-2 text-xs">
            <div>
              <div class="text-gray-500 dark:text-gray-400">Size</div>
              <div class="font-medium text-gray-900 dark:text-white">{{ failedFiles.totalMB.toFixed(1) }} MB</div>
            </div>
            <div>
              <div class="text-gray-500 dark:text-gray-400">Failover</div>
              <div class="font-medium text-gray-900 dark:text-white">{{ failedFiles.failoverCount }}</div>
            </div>
            <div>
              <div class="text-gray-500 dark:text-gray-400">QoS 0</div>
              <div class="font-medium text-gray-900 dark:text-white">{{ failedFiles.qos0Count }}</div>
            </div>
          </div>
        </div>
        
        <!-- No Failed Files -->
        <div v-else class="border-t border-gray-200 dark:border-gray-700 pt-2">
          <div class="flex items-center gap-1.5 text-xs text-green-600 dark:text-green-400">
            <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            No failed files
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue';
import { systemApi } from '../api/system';
import LoadingSpinner from './common/LoadingSpinner.vue';

const loading = ref(true);
const error = ref(null);
const maintenanceMode = ref(false);
const bufferedMessages = ref(0);
const bufferHealthy = ref(true);
const bufferStats = ref(null);

const failedFiles = computed(() => {
  if (!bufferStats.value?.failedFiles) {
    return { count: 0, totalMB: 0, failoverCount: 0, qos0Count: 0 };
  }
  return bufferStats.value.failedFiles;
});

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    const response = await systemApi.getMaintenanceStatus();
    maintenanceMode.value = response.data.maintenanceMode;
    bufferedMessages.value = response.data.bufferedMessages || 0;
    bufferHealthy.value = response.data.bufferHealthy !== false;
    bufferStats.value = response.data.bufferStats || null;
  } catch (err) {
    error.value = err.message;
  } finally {
    loading.value = false;
  }
}

function formatNumber(num) {
  if (num >= 1000000) return `${(num / 1000000).toFixed(1)}M`;
  if (num >= 1000) return `${(num / 1000).toFixed(1)}K`;
  return num.toString();
}

onMounted(() => {
  loadData();
});
</script>

<style scoped>
.compact-card {
  @apply max-w-full;
}

.chart-header-compact {
  @apply px-4 py-2.5 border-b border-gray-200/40 dark:border-gray-800/40 flex items-center justify-between;
}

.chart-title-compact {
  @apply text-sm font-semibold text-gray-900 dark:text-white tracking-tight;
}

.chart-body-compact {
  @apply p-3;
}

.stat-box-compact {
  @apply p-2 rounded-lg bg-gray-50 dark:bg-gray-800/50;
}

.stat-label-compact {
  @apply text-xs font-medium text-gray-500 dark:text-gray-400 mb-0.5;
}

.stat-value-compact {
  @apply text-base font-semibold;
}
</style>

