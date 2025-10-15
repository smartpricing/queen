<template>
  <div class="p-4 sm:p-6">
    <div class="space-y-4 sm:space-y-6 max-w-7xl mx-auto">
      <!-- Filters -->
      <AnalyticsFilters
        v-model:time-range="timeRange"
        v-model:queue="queueFilter"
        v-model:namespace="namespaceFilter"
        v-model:task="taskFilter"
        :queues="allQueues"
        :namespaces="namespaces"
        :tasks="tasks"
      />

      <LoadingSpinner v-if="loading && !statusData" />

      <div v-else-if="error" class="card bg-red-50 dark:bg-red-900/20 text-red-600 text-sm">
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
        <div class="card">
          <h3 class="text-base font-semibold mb-4 text-gray-900 dark:text-gray-100">Message Flow Over Time</h3>
          <MessageFlowChart :data="statusData" />
        </div>

        <!-- Charts Row -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
          <!-- Top Queues Chart -->
          <div class="card">
            <h3 class="text-base font-semibold mb-4 text-gray-900 dark:text-gray-100">Top Queues by Volume</h3>
            <TopQueuesChart :queues="filteredQueues" />
          </div>
          
          <!-- Message Distribution Chart -->
          <div class="card">
            <h3 class="text-base font-semibold mb-4 text-gray-900 dark:text-gray-100">Message Status Distribution</h3>
            <MessageDistributionChart :messages="statusData?.messages" />
          </div>
        </div>

        <!-- Performance Metrics -->
        <div class="card">
          <h3 class="text-base font-semibold mb-4 text-gray-900 dark:text-gray-100">Performance Metrics</h3>
          <AnalyticsMetrics :data="statusData" :messages="statusData?.messages" />
        </div>

        <!-- Detailed Stats -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
          <!-- Leases Info -->
          <div class="card">
            <h3 class="text-base font-semibold mb-4">Active Leases</h3>
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
          <div class="card">
            <h3 class="text-base font-semibold mb-4">Dead Letter Queue</h3>
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
            
            <!-- Top Errors -->
            <div v-if="statusData?.deadLetterQueue?.topErrors?.length" class="mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
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

// Filter queues based on current filters
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
    // Calculate time range
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
    
    // Add filters if selected
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
  
  // Register refresh callback for header button
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
