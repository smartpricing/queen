<template>
  <div class="dashboard-container">
    <div class="p-4 sm:p-6">
      <div class="space-y-5 sm:space-y-7 max-w-7xl mx-auto">
        <LoadingSpinner v-if="loading && !overview" />

        <div v-else-if="error" class="card bg-red-50 dark:bg-red-900/20 text-red-600 text-sm">
          <p><strong>Error loading dashboard:</strong> {{ error }}</p>
        </div>

        <template v-else>
          <!-- Metric Cards with Icons -->
          <div class="grid grid-cols-2 lg:grid-cols-4 gap-4 sm:gap-5">
            <div class="metric-card-enhanced">
              <div class="flex items-start justify-between">
                <div class="flex-1">
                  <p class="metric-label">Queues</p>
                  <p class="metric-value-enhanced">{{ formatNumber(overview?.queues || 0) }}</p>
                  <p class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">
                    Partitions: <span class="font-semibold">{{ formatNumber(overview?.partitions || 0) }}</span>
                  </p>
                </div>
                <div class="metric-icon bg-gradient-to-br from-rose-500/20 to-purple-500/20">
                  <svg class="w-6 h-6 text-rose-600 dark:text-rose-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6h16M4 10h16M4 14h16M4 18h16" />
                  </svg>
                </div>
              </div>
            </div>

            <div class="metric-card-enhanced">
              <div class="flex items-start justify-between">
                <div class="flex-1">
                  <p class="metric-label">Pending</p>
                  <p class="metric-value-enhanced">{{ formatNumber(calculatedPending) }}</p>
                  <p class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">
                    Processing: <span class="font-semibold">{{ formatNumber(overview?.messages?.processing || 0) }}</span>
                  </p>
                </div>
                <div class="metric-icon bg-gradient-to-br from-yellow-500/20 to-orange-500/20">
                  <svg class="w-6 h-6 text-yellow-600 dark:text-yellow-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
                  </svg>
                </div>
              </div>
            </div>

            <div class="metric-card-enhanced">
              <div class="flex items-start justify-between">
                <div class="flex-1">
                  <p class="metric-label">Completed</p>
                  <p class="metric-value-enhanced text-green-600 dark:text-green-400">{{ formatNumber(overview?.messages?.completed || 0) }}</p>
                  <p class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">
                    Total in DB: <span class="font-semibold">{{ formatNumber(overview?.messages?.total || 0) }}</span>
                  </p>
                </div>
                <div class="metric-icon bg-gradient-to-br from-green-500/20 to-emerald-500/20">
                  <svg class="w-6 h-6 text-green-600 dark:text-green-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
                  </svg>
                </div>
              </div>
            </div>

            <div class="metric-card-enhanced">
              <div class="flex items-start justify-between">
                <div class="flex-1">
                  <p class="metric-label">Failed</p>
                  <p class="metric-value-enhanced text-red-600 dark:text-red-400">{{ formatNumber(overview?.messages?.failed || 0) }}</p>
                  <p class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">
                    Dead Letter: <span class="font-semibold">{{ formatNumber(overview?.messages?.deadLetter || 0) }}</span>
                  </p>
                </div>
                <div class="metric-icon bg-gradient-to-br from-red-500/20 to-rose-500/20">
                  <svg class="w-6 h-6 text-red-600 dark:text-red-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                  </svg>
                </div>
              </div>
            </div>
          </div>

          <!-- Throughput Chart - Enhanced -->
          <div class="chart-card-enhanced">
            <div class="flex items-center justify-between mb-5">
              <div class="flex items-center gap-2">
                <div class="w-8 h-8 rounded-lg bg-gradient-to-br from-rose-500/20 to-purple-500/20 flex items-center justify-center">
                  <svg class="w-5 h-5 text-rose-600 dark:text-rose-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 12l3-3 3 3 4-4M8 21l4-4 4 4M3 4h18M4 4h16v12a1 1 0 01-1 1H5a1 1 0 01-1-1V4z" />
                  </svg>
                </div>
                <h3 class="text-base font-semibold text-gray-900 dark:text-gray-100">Message Throughput</h3>
              </div>
              <span class="text-xs text-gray-500 dark:text-gray-400 px-2 py-1 bg-gray-100 dark:bg-slate-700 rounded-full">
                Last Hour
              </span>
            </div>
            <div class="chart-wrapper">
              <ThroughputChart :data="status" />
            </div>
          </div>

          <!-- Stats Row - Enhanced -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-5 sm:gap-6">
            <div class="info-card-enhanced">
              <div class="flex items-center gap-2 mb-4">
                <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-purple-500/20 to-indigo-500/20 flex items-center justify-center">
                  <svg class="w-4 h-4 text-purple-600 dark:text-purple-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
                  </svg>
                </div>
                <h3 class="text-base font-semibold">Message Status</h3>
              </div>
              <MessageStatusCard :data="overview?.messages" :calculated-pending="calculatedPending" />
            </div>

            <div class="info-card-enhanced">
              <div class="flex items-center gap-2 mb-4">
                <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-blue-500/20 to-cyan-500/20 flex items-center justify-center">
                  <svg class="w-4 h-4 text-blue-600 dark:text-blue-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
                  </svg>
                </div>
                <h3 class="text-base font-semibold">System Performance</h3>
              </div>
              <PerformanceCard :data="metrics" />
            </div>
          </div>

          <!-- Top Queues - Enhanced -->
          <div class="table-card-enhanced">
            <div class="flex items-center gap-2 mb-4">
              <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-indigo-500/20 to-violet-500/20 flex items-center justify-center">
                <svg class="w-4 h-4 text-indigo-600 dark:text-indigo-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19V6l12-3v13M9 19c0 1.105-1.343 2-3 2s-3-.895-3-2 1.343-2 3-2 3 .895 3 2zm12-3c0 1.105-1.343 2-3 2s-3-.895-3-2 1.343-2 3-2 3 .895 3 2zM9 10l12-3" />
                </svg>
              </div>
              <h3 class="text-base font-semibold text-gray-900 dark:text-gray-100">Top Queues by Activity</h3>
            </div>
            <TopQueuesTable :queues="topQueues" />
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { healthApi } from '../api/health';
import { resourcesApi } from '../api/resources';
import { analyticsApi } from '../api/analytics';
import { queuesApi } from '../api/queues';
import { formatNumber } from '../utils/formatters';

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

<style scoped>
.dashboard-container {
  background: radial-gradient(ellipse at top, rgba(244, 63, 94, 0.03) 0%, transparent 50%),
              radial-gradient(ellipse at bottom right, rgba(168, 85, 247, 0.03) 0%, transparent 50%);
  min-height: 100%;
}

.metric-card-enhanced {
  position: relative;
  background: linear-gradient(to bottom, 
    rgba(255, 255, 255, 1) 0%, 
    rgba(255, 255, 255, 0.98) 100%
  );
  border: 1px solid theme('colors.gray.200');
  border-radius: 0.75rem;
  padding: 1.25rem;
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  overflow: hidden;
}

.dark .metric-card-enhanced {
  background: linear-gradient(to bottom, 
    rgba(30, 41, 59, 1) 0%, 
    rgba(30, 41, 59, 0.95) 100%
  );
  border-color: theme('colors.gray.700');
}


.metric-card-enhanced:hover {
  transform: translateY(-4px);
  box-shadow: 
    0 10px 15px -3px rgba(0, 0, 0, 0.1),
    0 4px 6px -4px rgba(0, 0, 0, 0.1),
    0 0 20px 0 rgba(244, 63, 94, 0.1),
    0 0 20px 0 rgba(168, 85, 247, 0.1);
}

.metric-icon {
  width: 3rem;
  height: 3rem;
  border-radius: 0.75rem;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.3s ease;
}

.metric-card-enhanced:hover .metric-icon {
  transform: scale(1.1) rotate(5deg);
}

.metric-value-enhanced {
  font-size: 2rem;
  font-weight: 700;
  line-height: 1.2;
  margin-top: 0.5rem;
  background: linear-gradient(135deg, #f43f5e 0%, #ec4899 50%, #a855f7 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  letter-spacing: -0.02em;
}

.chart-card-enhanced {
  position: relative;
  background: white;
  border: 1px solid theme('colors.gray.200');
  border-radius: 1rem;
  padding: 1.5rem;
  transition: all 0.3s ease;
  box-shadow: 
    0 1px 3px 0 rgba(0, 0, 0, 0.1),
    0 0 15px 0 rgba(244, 63, 94, 0.03);
}

.dark .chart-card-enhanced {
  background: linear-gradient(to bottom right,
    rgba(30, 41, 59, 1) 0%,
    rgba(30, 41, 59, 0.98) 100%
  );
  border-color: theme('colors.gray.700');
  box-shadow: 
    0 1px 3px 0 rgba(0, 0, 0, 0.3),
    0 0 20px 0 rgba(168, 85, 247, 0.05);
}

.chart-card-enhanced:hover {
  box-shadow: 
    0 4px 6px -1px rgba(0, 0, 0, 0.1),
    0 0 25px 0 rgba(244, 63, 94, 0.08),
    0 0 25px 0 rgba(168, 85, 247, 0.08);
}

.chart-wrapper {
  position: relative;
  background: radial-gradient(ellipse at center,
    rgba(244, 63, 94, 0.02) 0%,
    rgba(168, 85, 247, 0.02) 50%,
    transparent 100%
  );
  border-radius: 0.5rem;
  padding: 0.5rem;
}

.dark .chart-wrapper {
  background: radial-gradient(ellipse at center,
    rgba(244, 63, 94, 0.05) 0%,
    rgba(168, 85, 247, 0.05) 50%,
    transparent 100%
  );
}

.info-card-enhanced {
  position: relative;
  background: white;
  border: 1px solid theme('colors.gray.200');
  border-radius: 0.75rem;
  padding: 1.25rem;
  transition: all 0.3s ease;
  overflow: hidden;
}

.dark .info-card-enhanced {
  background: rgba(30, 41, 59, 0.8);
  border-color: theme('colors.gray.700');
}

.info-card-enhanced::after {
  content: '';
  position: absolute;
  top: 0;
  right: 0;
  width: 100px;
  height: 100px;
  background: radial-gradient(circle,
    rgba(168, 85, 247, 0.1) 0%,
    transparent 70%
  );
  pointer-events: none;
}

.info-card-enhanced:hover {
  box-shadow: 
    0 4px 6px -1px rgba(0, 0, 0, 0.1),
    0 0 15px 0 rgba(168, 85, 247, 0.1);
}

.table-card-enhanced {
  position: relative;
  background: white;
  border: 1px solid theme('colors.gray.200');
  border-radius: 0.75rem;
  padding: 1.25rem;
  transition: all 0.3s ease;
}

.dark .table-card-enhanced {
  background: linear-gradient(135deg,
    rgba(30, 41, 59, 1) 0%,
    rgba(30, 41, 59, 0.95) 100%
  );
  border-color: theme('colors.gray.700');
}

.table-card-enhanced:hover {
  box-shadow: 
    0 4px 6px -1px rgba(0, 0, 0, 0.1),
    0 0 15px 0 rgba(99, 102, 241, 0.1);
}

</style>
