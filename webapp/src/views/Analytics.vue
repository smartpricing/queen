<template>
  <div class="page-flat">
    <div class="py-4 px-3">
      <div class="space-y-2.5">
        <!-- Filters -->
        <div class="filter-flat">
          <AnalyticsFilters
          v-model:time-range="timeRange"
          v-model:queue="queueFilter"
          v-model:namespace="namespaceFilter"
          v-model:task="taskFilter"
          :queues="allQueues"
          :namespaces="namespaces"
          :tasks="tasks"
          />
        </div>

        <LoadingSpinner v-if="loading && !statusData" />

        <div v-else-if="error" class="error-flat">
          <p><strong>Error loading analytics:</strong> {{ error }}</p>
        </div>

        <template v-else>
          <!-- Active Filters Display -->
          <div v-if="queueFilter || namespaceFilter || taskFilter" class="flex flex-wrap gap-2">
            <span v-if="queueFilter" class="badge badge-info">
              Queue: {{ queueFilter }}
            </span>
            <span v-if="namespaceFilter" class="badge badge-info">
              Namespace: {{ namespaceFilter }}
            </span>
            <span v-if="taskFilter" class="badge badge-info">
              Task: {{ taskFilter }}
            </span>
          </div>

          <!-- Message Flow Chart -->
          <div class="chart-flat">
            <div class="flex items-center gap-2 mb-2">
              <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-rose-500/10 to-purple-500/20 flex items-center justify-center">
                <svg class="w-4 h-4 text-rose-600 dark:text-rose-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 12l3-3 3 3 4-4M8 21l4-4 4 4M3 4h18M4 4h16v12a1 1 0 01-1 1H5a1 1 0 01-1-1V4z" />
                </svg>
              </div>
              <h3 class="text-sm font-semibold text-gray-900 dark:text-gray-100">Message Flow Over Time</h3>
            </div>
            <MessageFlowChart :data="statusData" />
          </div>

          <!-- Charts Row -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-2">
            <!-- Top Queues Chart -->
            <div class="chart-flat">
              <div class="flex items-center gap-2 mb-2">
                <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-indigo-500/10 to-violet-500/20 flex items-center justify-center">
                  <svg class="w-4 h-4 text-indigo-600 dark:text-indigo-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
                  </svg>
                </div>
                <h3 class="text-sm font-semibold text-gray-900 dark:text-gray-100">Top Queues by Volume</h3>
              </div>
              <TopQueuesChart :queues="filteredQueues" />
            </div>
            
            <!-- Message Distribution Chart -->
            <div class="chart-flat">
              <div class="flex items-center gap-2 mb-2">
                <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-green-500/10 to-emerald-500/20 flex items-center justify-center">
                  <svg class="w-4 h-4 text-green-600 dark:text-green-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 3.055A9.001 9.001 0 1020.945 13H11V3.055z" />
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20.488 9H15V3.512A9.025 9.025 0 0120.488 9z" />
                  </svg>
                </div>
                <h3 class="text-sm font-semibold text-gray-900 dark:text-gray-100">Message Status Distribution</h3>
              </div>
              <MessageDistributionChart :messages="statusData?.messages" />
            </div>
          </div>

          <!-- Performance Metrics -->
          <div class="info-flat">
            <div class="flex items-center gap-2 mb-2">
              <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-blue-500/10 to-cyan-500/20 flex items-center justify-center">
                <svg class="w-4 h-4 text-blue-600 dark:text-blue-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
                </svg>
              </div>
              <h3 class="text-sm font-semibold text-gray-900 dark:text-gray-100">Performance Metrics</h3>
            </div>
            <AnalyticsMetrics :data="statusData" :messages="statusData?.messages" />
          </div>

          <!-- Detailed Stats -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-2">
            <!-- Leases Info -->
            <div class="info-flat">
              <div class="flex items-center gap-2 mb-2">
                <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-purple-500/10 to-indigo-500/20 flex items-center justify-center">
                  <svg class="w-4 h-4 text-purple-600 dark:text-purple-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z" />
                  </svg>
                </div>
                <h3 class="text-sm font-semibold">Active Leases</h3>
              </div>
              <div class="space-y-2.5">
                <div class="flex items-center justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Active Leases</span>
                  <span class="font-semibold">{{ statusData?.leases?.active || 0 }}</span>
                </div>
                <div class="flex items-center justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Partitions with Leases</span>
                  <span class="font-semibold">{{ statusData?.leases?.partitionsWithLeases || 0 }}</span>
                </div>
                <div class="flex items-center justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Total Batch Size</span>
                  <span class="font-semibold">{{ statusData?.leases?.totalBatchSize || 0 }}</span>
                </div>
                <div class="flex items-center justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Total Acked</span>
                  <span class="font-semibold">{{ statusData?.leases?.totalAcked || 0 }}</span>
                </div>
              </div>
            </div>

            <!-- Dead Letter Queue -->
            <div class="info-flat">
              <div class="flex items-center gap-2 mb-2">
                <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-red-500/10 to-rose-500/20 flex items-center justify-center">
                  <svg class="w-4 h-4 text-red-600 dark:text-red-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                  </svg>
                </div>
                <h3 class="text-sm font-semibold">Dead Letter Queue</h3>
              </div>
              <div class="space-y-2.5">
                <div class="flex items-center justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Total Messages</span>
                  <span class="font-semibold">{{ statusData?.deadLetterQueue?.totalMessages || 0 }}</span>
                </div>
                <div class="flex items-center justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Affected Partitions</span>
                  <span class="font-semibold">{{ statusData?.deadLetterQueue?.affectedPartitions || 0 }}</span>
                </div>
              </div>
              
              <div v-if="statusData?.deadLetterQueue?.topErrors?.length" class="mt-4 pt-4 border-t border-gray-200/30 dark:border-gray-700/30">
                <p class="text-xs font-semibold text-gray-600 dark:text-gray-400 mb-2">Top Errors:</p>
                <div class="space-y-2">
                  <div
                    v-for="(errorItem, idx) in statusData.deadLetterQueue.topErrors.slice(0, 3)"
                    :key="idx"
                    class="text-xs"
                  >
                    <div class="flex items-center justify-between">
                      <span class="text-gray-600 dark:text-gray-400 truncate flex-1">{{ errorItem.error }}</span>
                      <span class="font-semibold ml-2">{{ errorItem.count }}</span>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import { analyticsApi } from '../api/analytics';
import { queuesApi } from '../api/queues';
import { resourcesApi } from '../api/resources';

import AnalyticsFilters from '../components/analytics/AnalyticsFilters.vue';
import MessageFlowChart from '../components/analytics/MessageFlowChart.vue';
import TopQueuesChart from '../components/analytics/TopQueuesChart.vue';
import MessageDistributionChart from '../components/analytics/MessageDistributionChart.vue';
import AnalyticsMetrics from '../components/analytics/AnalyticsMetrics.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';

const loading = ref(false);
const error = ref(null);
const timeRange = ref('1h');
const queueFilter = ref('');
const namespaceFilter = ref('');
const taskFilter = ref('');
const statusData = ref(null);
const allQueues = ref([]);
const namespaces = ref([]);
const tasks = ref([]);

const filteredQueues = computed(() => {
  let queues = allQueues.value;
  
  if (queueFilter.value) {
    queues = queues.filter(q => q.name === queueFilter.value);
  }
  if (namespaceFilter.value) {
    queues = queues.filter(q => q.namespace === namespaceFilter.value);
  }
  if (taskFilter.value) {
    queues = queues.filter(q => q.task === taskFilter.value);
  }
  
  return queues;
});

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    const now = new Date();
    const from = new Date(now);
    
    switch (timeRange.value) {
      case '1h':
        from.setHours(from.getHours() - 1);
        break;
      case '6h':
        from.setHours(from.getHours() - 6);
        break;
      case '24h':
        from.setHours(from.getHours() - 24);
        break;
      case '7d':
        from.setDate(from.getDate() - 7);
        break;
    }
    
    const params = {
      from: from.toISOString(),
      to: now.toISOString(),
    };
    
    if (queueFilter.value) params.queue = queueFilter.value;
    if (namespaceFilter.value) params.namespace = namespaceFilter.value;
    if (taskFilter.value) params.task = taskFilter.value;
    
    const [statusRes, queuesRes, namespacesRes, tasksRes] = await Promise.all([
      analyticsApi.getStatus(params),
      queuesApi.getQueues(),
      resourcesApi.getNamespaces(),
      resourcesApi.getTasks(),
    ]);
    
    statusData.value = statusRes.data;
    allQueues.value = queuesRes.data.queues;
    namespaces.value = namespacesRes.data.namespaces;
    tasks.value = tasksRes.data.tasks;
  } catch (err) {
    error.value = err.message;
    console.error('Analytics error:', err);
  } finally {
    loading.value = false;
  }
}

watch([timeRange, queueFilter, namespaceFilter, taskFilter], () => {
  loadData();
});

onMounted(() => {
  loadData();
  
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/analytics', loadData);
  }
});

onUnmounted(() => {
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/analytics', null);
  }
});
</script>

<style scoped>
.page-flat {
  min-height: 100%;
}

.chart-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  transition: all 0.3s ease;
}

.dark .chart-flat {
  background: #0a0d14;
}

.chart-flat:hover {
  background: #fafafa;
}

.dark .chart-flat:hover {
  background: #0d1117;
}

/* Flat filters */
.filter-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
}

.dark .filter-flat {
  background: #0a0d14;
}


.filter-flat :deep(.input) {
  background: transparent;
  border: 1px solid rgba(156, 163, 175, 0.15);
  transition: all 0.2s ease;
}

.filter-flat :deep(.input:hover) {
  border-color: rgba(156, 163, 175, 0.25);
}

.filter-flat :deep(.input:focus) {
  background: rgba(244, 63, 94, 0.02);
  border-color: rgba(244, 63, 94, 0.4);
  box-shadow: 0 0 0 3px rgba(244, 63, 94, 0.05);
}

.dark .filter-flat :deep(.input) {
  border-color: rgba(156, 163, 175, 0.1);
}

.dark .filter-flat :deep(.input:focus) {
  background: rgba(244, 63, 94, 0.03);
  border-color: rgba(244, 63, 94, 0.5);
  box-shadow: 0 0 0 3px rgba(244, 63, 94, 0.08);
}

.info-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  transition: all 0.3s ease;
}

.dark .info-flat {
  background: #0a0d14;
}

.info-flat:hover {
  background: #fafafa;
}

.dark .info-flat:hover {
  background: #0d1117;
}

.error-flat {
  background: transparent;
  color: #dc2626;
  font-size: 0.875rem;
  padding: 1rem;
}

.dark .error-flat {
  color: #fca5a5;
}
</style>
