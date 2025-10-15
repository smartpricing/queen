<template>
  <div class="p-4 sm:p-6">
    <div class="space-y-4 sm:space-y-6 max-w-7xl mx-auto">
      <LoadingSpinner v-if="loading && !overview" />

      <div v-else-if="error" class="card bg-red-50 dark:bg-red-900/20 text-red-600 text-sm">
        <p><strong>Error loading dashboard:</strong> {{ error }}</p>
      </div>

      <template v-else>
        <!-- Metric Cards -->
        <div class="grid grid-cols-2 lg:grid-cols-4 gap-3 sm:gap-4">
          <MetricCard
            title="Queues"
            :value="overview?.queues || 0"
            :secondary-value="overview?.partitions || 0"
            secondary-label="Partitions"
          />
          <MetricCard
            title="Pending"
            :value="calculatedPending"
            :secondary-value="overview?.messages?.processing || 0"
            secondary-label="Processing"
          />
          <MetricCard
            title="Completed"
            :value="overview?.messages?.completed || 0"
            :secondary-value="overview?.messages?.total || 0"
            secondary-label="Total in DB"
          />
          <MetricCard
            title="Failed"
            :value="overview?.messages?.failed || 0"
            :secondary-value="overview?.messages?.deadLetter || 0"
            secondary-label="Dead Letter"
          />
        </div>

        <!-- Throughput Chart -->
        <div class="card">
          <h3 class="text-base font-semibold mb-4 text-gray-900 dark:text-gray-100">Message Throughput</h3>
          <ThroughputChart :data="status" />
        </div>

        <!-- Stats Row -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
          <MessageStatusCard :data="overview?.messages" :calculated-pending="calculatedPending" />
          <PerformanceCard :data="metrics" />
        </div>

        <!-- Top Queues -->
        <div class="card">
          <h3 class="text-base font-semibold mb-4 text-gray-900 dark:text-gray-100">Top Queues by Activity</h3>
          <TopQueuesTable :queues="topQueues" />
        </div>
      </template>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { healthApi } from '../api/health';
import { resourcesApi } from '../api/resources';
import { analyticsApi } from '../api/analytics';
import { queuesApi } from '../api/queues';

import MetricCard from '../components/common/MetricCard.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import ThroughputChart from '../components/dashboard/ThroughputChart.vue';
import MessageStatusCard from '../components/dashboard/MessageStatusCard.vue';
import PerformanceCard from '../components/dashboard/PerformanceCard.vue';
import TopQueuesTable from '../components/dashboard/TopQueuesTable.vue';

const loading = ref(false);
const error = ref(null);
const overview = ref(null);
const health = ref(null);
const metrics = ref(null);
const status = ref(null);
const topQueues = ref([]);

// Calculate pending as: total - completed - failed - deadLetter
const calculatedPending = computed(() => {
  if (!overview.value?.messages) return 0;
  
  const total = overview.value.messages.total || 0;
  const completed = overview.value.messages.completed || 0;
  const failed = overview.value.messages.failed || 0;
  const deadLetter = overview.value.messages.deadLetter || 0;
  const processing = overview.value.messages.processing || 0;
  
  // Pending = Total - (Completed + Failed + DLQ + Processing)
  const pending = total - completed - failed - deadLetter - processing;
  
  return Math.max(0, pending); // Ensure non-negative
});

async function loadData() {
  loading.value = true;
  error.value = null;

  try {
    const [overviewRes, healthRes, metricsRes, statusRes, queuesRes] = await Promise.all([
      resourcesApi.getOverview(),
      healthApi.getHealth(),
      healthApi.getMetrics(),
      analyticsApi.getStatus(),
      queuesApi.getQueues(),
    ]);

    overview.value = overviewRes.data;
    health.value = healthRes.data;
    metrics.value = metricsRes.data;
    status.value = statusRes.data;
    
    // Get top 5 queues by total messages
    topQueues.value = queuesRes.data.queues
      .sort((a, b) => (b.messages?.total || 0) - (a.messages?.total || 0))
      .slice(0, 5);
  } catch (err) {
    error.value = err.message;
    console.error('Dashboard error:', err);
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadData();
  
  // Register refresh callback for header button
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/', loadData);
  }
});

onUnmounted(() => {
  // Clean up callback
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/', null);
  }
});
</script>
