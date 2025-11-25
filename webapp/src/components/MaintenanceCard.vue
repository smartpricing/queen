<template>
  <div class="system-status-banner">
    <LoadingSpinner v-if="loading" class="w-4 h-4" />
    
    <div v-else-if="error" class="flex items-center gap-2 text-sm text-red-600 dark:text-red-400">
      <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4m0 4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
      </svg>
      {{ error }}
    </div>
    
    <div v-else class="flex items-center justify-between w-full">
      <!-- Left: System Status -->
      <div class="flex items-center gap-3">
        <div class="flex items-center gap-2">
          <span class="text-sm font-semibold text-gray-900 dark:text-white">System Status:</span>
          <span 
            v-if="maintenanceMode" 
            class="inline-flex items-center px-2 py-1 rounded text-xs font-medium bg-yellow-100 text-yellow-800 dark:bg-yellow-900/30 dark:text-yellow-400"
          >
            Maintenance Mode
          </span>
          <span 
            v-else
            class="inline-flex items-center px-2 py-1 rounded text-xs font-medium bg-green-100 text-green-800 dark:bg-green-900/30 dark:text-green-400"
          >
            Operational
          </span>
        </div>
      </div>
      
      <!-- Right: Stats -->
      <div class="flex items-center gap-6 text-xs">
        <div class="flex items-center gap-1.5">
          <span class="text-gray-500 dark:text-gray-400">Buffered:</span>
          <span class="font-semibold" :class="bufferedMessages > 0 ? 'text-yellow-600 dark:text-yellow-400' : 'text-gray-900 dark:text-gray-100'">
            {{ formatNumber(bufferedMessages) }}
          </span>
        </div>
        
        <div class="flex items-center gap-1.5">
          <span class="text-gray-500 dark:text-gray-400">Database:</span>
          <span class="font-semibold" :class="bufferHealthy ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'">
            {{ bufferHealthy ? 'Healthy' : 'Down' }}
          </span>
        </div>
        
        <div v-if="failedFiles.count > 0" class="flex items-center gap-1.5">
          <span class="text-gray-500 dark:text-gray-400">Failed Files:</span>
          <span class="font-semibold text-red-600 dark:text-red-400">
            {{ failedFiles.count }}
          </span>
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
.system-status-banner {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl px-5 py-3 flex items-center;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
}

.dark .system-status-banner {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}
</style>

