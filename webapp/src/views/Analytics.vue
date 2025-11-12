<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <!-- Filters -->
        <div class="filter-card">
          <AnalyticsFilters
          v-model:time-range="timeRange"
          v-model:custom-mode="customMode"
          v-model:custom-from="customFrom"
          v-model:custom-to="customTo"
          v-model:queue="queueFilter"
          v-model:namespace="namespaceFilter"
          v-model:task="taskFilter"
          :queues="allQueues"
          :namespaces="namespaces"
          :tasks="tasks"
          @apply-custom-range="applyCustomRange"
          />
        </div>

        <LoadingSpinner v-if="loading && !statusData" />

        <div v-else-if="error" class="error-card">
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
          <div class="chart-card-professional">
            <div class="chart-header">
              <h3 class="chart-title">Message Flow Over Time</h3>
            </div>
            <div class="chart-body">
              <MessageFlowChart :data="statusData" />
            </div>
          </div>

          <!-- Charts Row -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-5">
            <!-- Top Queues Chart -->
            <div class="chart-card-professional">
              <div class="chart-header">
                <h3 class="chart-title">Top Queues by Volume</h3>
              </div>
              <div class="chart-body">
                <TopQueuesChart :queues="filteredQueues" />
              </div>
            </div>
            
            <!-- Message Distribution Chart -->
            <div class="chart-card-professional">
              <div class="chart-header">
                <h3 class="chart-title">Message Status Distribution</h3>
              </div>
              <div class="chart-body">
                <MessageDistributionChart :messages="statusData?.messages" />
              </div>
            </div>
          </div>

          <!-- Performance Metrics -->
          <div class="info-card-white">
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
            <div class="info-card-white">
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
            <div class="info-card-white">
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
const customMode = ref(false);
const customFrom = ref('');
const customTo = ref('');
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

function formatDateTimeLocal(date) {
  // Format: yyyy-MM-ddTHH:mm
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  return `${year}-${month}-${day}T${hours}:${minutes}`;
}

function applyCustomRange() {
  if (!customFrom.value || !customTo.value) {
    error.value = 'Please select both FROM and TO dates';
    return;
  }
  
  const fromDate = new Date(customFrom.value);
  const toDate = new Date(customTo.value);
  
  if (fromDate >= toDate) {
    error.value = 'FROM date must be before TO date';
    return;
  }
  
  loadData();
}

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    let from, to;
    
    if (customMode.value && customFrom.value && customTo.value) {
      // Use custom range
      from = new Date(customFrom.value);
      to = new Date(customTo.value);
    } else {
      // Use quick range
      const now = new Date();
      from = new Date(now);
      
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
      
      to = now;
    }
    
    const params = {
      from: from.toISOString(),
      to: to.toISOString(),
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

// Watch for changes - handle both quick and custom modes
watch(timeRange, () => {
  if (!customMode.value) {
    loadData();
  }
});

watch([queueFilter, namespaceFilter, taskFilter], () => {
  loadData();
});

watch(customMode, (newValue) => {
  if (newValue) {
    // Initialize with current range when entering custom mode
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
    
    customTo.value = formatDateTimeLocal(now);
    customFrom.value = formatDateTimeLocal(from);
  }
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
/* Styles inherited from professional.css */
</style>
