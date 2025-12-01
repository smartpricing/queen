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
        <!-- Shared State / UDPSYNC -->
        <div v-if="sharedState?.enabled" class="flex items-center gap-1.5 pr-4 border-r border-gray-200 dark:border-gray-700" title="Connected peers">
          <svg class="w-3.5 h-3.5 text-violet-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
            <path stroke-linecap="round" stroke-linejoin="round" d="M7.5 21L3 16.5m0 0L7.5 12M3 16.5h13.5m0-13.5L21 7.5m0 0L16.5 12M21 7.5H7.5" />
          </svg>
          <span class="font-semibold text-violet-600 dark:text-violet-400">
            {{ sharedState.server_health?.servers_alive || 0 }}/{{ sharedState.transport?.peer_count || 0 }}
          </span>
        </div>
        
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
import { ref, onMounted, onUnmounted, computed } from 'vue';
import { systemApi } from '../api/system';
import { systemMetricsApi } from '../api/system-metrics';
import LoadingSpinner from './common/LoadingSpinner.vue';

const loading = ref(true);
const error = ref(null);
const maintenanceMode = ref(false);
const bufferedMessages = ref(0);
const bufferHealthy = ref(true);
const bufferStats = ref(null);
const sharedStateRaw = ref(null);
let refreshInterval = null;

const failedFiles = computed(() => {
  if (!bufferStats.value?.failedFiles) {
    return { count: 0, totalMB: 0, failoverCount: 0, qos0Count: 0 };
  }
  return bufferStats.value.failedFiles;
});

// Transform DB metrics format to display format
const sharedState = computed(() => {
  const ss = sharedStateRaw.value;
  if (!ss || !ss.enabled) return null;
  
  return {
    enabled: true,
    server_health: {
      servers_alive: ss.server_health?.alive || 0,
      servers_dead: ss.server_health?.dead || 0
    },
    transport: {
      peer_count: (ss.server_health?.alive || 0) + (ss.server_health?.dead || 0)
    }
  };
});

async function loadData() {
  // Only show loading on initial load
  if (!sharedStateRaw.value && !maintenanceMode.value) {
    loading.value = true;
  }
  error.value = null;
  
  try {
    // Fetch maintenance status and system metrics (DB) in parallel
    const [maintenanceRes, systemMetricsRes] = await Promise.all([
      systemApi.getMaintenanceStatus(),
      systemMetricsApi.getSystemMetrics().catch(() => ({ data: null }))
    ]);
    
    maintenanceMode.value = maintenanceRes.data.maintenanceMode;
    bufferedMessages.value = maintenanceRes.data.bufferedMessages || 0;
    bufferHealthy.value = maintenanceRes.data.bufferHealthy !== false;
    bufferStats.value = maintenanceRes.data.bufferStats || null;
    
    // Extract shared_state from the latest system metrics
    if (systemMetricsRes.data?.replicas?.length > 0) {
      const firstReplica = systemMetricsRes.data.replicas[0];
      if (firstReplica?.timeSeries?.length > 0) {
        const lastPoint = firstReplica.timeSeries[firstReplica.timeSeries.length - 1];
        sharedStateRaw.value = lastPoint?.metrics?.shared_state || null;
      }
    }
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
  // Auto-refresh every 5 seconds
  refreshInterval = setInterval(loadData, 5000);
});

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval);
  }
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

