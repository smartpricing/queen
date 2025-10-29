<template>
  <div class="dashboard-professional">
    <!-- Dashboard Content -->
    <div class="dashboard-content">
      <LoadingSpinner v-if="loading && !overview" />

      <div v-else-if="error" class="error-card">
        <p><strong>Error loading dashboard:</strong> {{ error }}</p>
      </div>

      <template v-else>
        <!-- Metric Cards - Professional -->
        <div class="metrics-grid">
          <!-- Queues Card -->
          <div class="metric-card-top metric-card-clickable" @click="navigateToQueues">
            <div class="flex items-center justify-between mb-0.5">
              <span class="metric-label">QUEUES</span>
              <svg class="w-4 h-4 text-gray-400 dark:text-gray-500 card-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
            </div>
            <div class="metric-value text-gray-600 dark:text-gray-300">{{ formatNumber(overview?.queues || 0) }}</div>
            <div class="metric-subtext-bottom">{{ formatNumber(overview?.partitions || 0) }} partitions</div>
          </div>

          <!-- Pending Card -->
          <div class="metric-card-top metric-card-clickable" @click="navigateToPending">
            <div class="flex items-center justify-between mb-0.5">
              <span class="metric-label">PENDING</span>
              <svg class="w-4 h-4 text-gray-400 dark:text-gray-500 card-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
            </div>
            <div class="metric-value text-orange-600 dark:text-orange-400">{{ formatNumber(calculatedPending) }}</div>
            <div class="metric-subtext-bottom">{{ formatNumber(overview?.messages?.processing || 0) }} processing</div>
          </div>

          <!-- Completed Card -->
          <div class="metric-card-top metric-card-clickable" @click="navigateToCompleted">
            <div class="flex items-center justify-between mb-0.5">
              <span class="metric-label">COMPLETED</span>
              <svg class="w-4 h-4 text-gray-400 dark:text-gray-500 card-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
            </div>
            <div class="metric-value text-emerald-600 dark:text-emerald-400">{{ formatNumber(overview?.messages?.completed || 0) }}</div>
            <div class="metric-subtext-bottom">{{ formatNumber(overview?.messages?.total || 0) }} total</div>
          </div>

          <!-- Failed Card -->
          <div class="metric-card-top metric-card-clickable" @click="navigateToFailed">
            <div class="flex items-center justify-between mb-0.5">
              <span class="metric-label">FAILED</span>
              <svg class="w-4 h-4 text-gray-400 dark:text-gray-500 card-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
            </div>
            <div class="metric-value text-red-600 dark:text-red-400">{{ formatNumber(overview?.messages?.failed || 0) }}</div>
            <div class="metric-subtext-bottom">{{ formatNumber(overview?.messages?.deadLetter || 0) }} DLQ</div>
          </div>
        </div>

        <!-- Charts Grid -->
        <div class="charts-grid">
          <!-- Throughput Chart -->
          <div class="chart-card chart-card-clickable" @click="navigateToAnalytics">
            <div class="chart-header">
              <div class="flex items-center gap-2">
                <h3 class="chart-title">Message Throughput</h3>
                <svg class="w-3.5 h-3.5 text-gray-400 dark:text-gray-500 card-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
                </svg>
              </div>
              <span class="chart-badge">Last Hour</span>
            </div>
            <div class="chart-body">
              <ThroughputChart :data="status" />
            </div>
          </div>

          <!-- Resource Usage Chart -->
          <div class="chart-card chart-card-clickable" @click="navigateToSystemMetrics">
            <div class="chart-header">
              <div class="flex items-center gap-2">
                <h3 class="chart-title">Resource Usage</h3>
                <svg class="w-3.5 h-3.5 text-gray-400 dark:text-gray-500 card-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
                </svg>
              </div>
              <span class="chart-badge">Last Hour</span>
            </div>
            <div class="chart-body">
              <ResourceUsageChart :data="systemMetrics" />
            </div>
          </div>
        </div>

        <!-- Queue Metrics Chart -->
        <div class="chart-card chart-card-clickable" @click="navigateToSystemMetrics">
          <div class="chart-header">
            <div class="flex items-center gap-2">
              <h3 class="chart-title">Queue & Connection Metrics</h3>
              <svg class="w-3.5 h-3.5 text-gray-400 dark:text-gray-500 card-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
            </div>
            <span class="chart-badge">Last Hour</span>
          </div>
          <div class="chart-body">
            <QueueMetricsChart :data="systemMetrics" />
          </div>
        </div>

        <!-- Top Queues Table -->
        <div class="chart-card">
          <div class="chart-header">
            <h3 class="chart-title">Top Queues by Activity</h3>
          </div>
          <div class="chart-body">
            <TopQueuesTable :queues="topQueues" />
          </div>
        </div>
      </template>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import { resourcesApi } from '../api/resources';
import { analyticsApi } from '../api/analytics';
import { queuesApi } from '../api/queues';
import { systemMetricsApi } from '../api/system-metrics';
import { formatNumber } from '../utils/formatters';

import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import ThroughputChart from '../components/dashboard/ThroughputChart.vue';
import ResourceUsageChart from '../components/dashboard/ResourceUsageChart.vue';
import QueueMetricsChart from '../components/dashboard/QueueMetricsChart.vue';
import TopQueuesTable from '../components/dashboard/TopQueuesTable.vue';

const router = useRouter();

const loading = ref(false);
const error = ref(null);
const overview = ref(null);
const status = ref(null);
const systemMetrics = ref(null);
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
    const [overviewRes, statusRes, queuesRes, systemMetricsRes] = await Promise.all([
      resourcesApi.getOverview(),
      analyticsApi.getStatus(),
      queuesApi.getQueues(),
      systemMetricsApi.getSystemMetrics(), // Fetch last hour of system metrics
    ]);

    overview.value = overviewRes.data;
    status.value = statusRes.data;
    systemMetrics.value = systemMetricsRes.data;
    
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

// Navigation functions
function navigateToQueues() {
  router.push('/queues');
}

function navigateToPending() {
  router.push('/messages?status=pending');
}

function navigateToCompleted() {
  router.push('/messages?status=completed');
}

function navigateToFailed() {
  router.push('/messages?status=failed');
}

function navigateToAnalytics() {
  router.push('/analytics');
}

function navigateToSystemMetrics() {
  router.push('/system-metrics');
}
</script>

<style scoped>
/* Professional Dashboard Design - Condoktur inspired */

.dashboard-professional {
  @apply min-h-screen bg-gray-50 dark:bg-[#0d1117];
  background-image: 
    radial-gradient(at 0% 0%, rgba(59, 130, 246, 0.03) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.03) 0px, transparent 50%);
}

.dark .dashboard-professional {
  background-image: 
    radial-gradient(at 0% 0%, rgba(59, 130, 246, 0.05) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.05) 0px, transparent 50%);
}

.dashboard-content {
  @apply px-6 lg:px-8 py-6 space-y-6;
}

/* Metrics Grid */
.metrics-grid {
  @apply grid grid-cols-2 lg:grid-cols-4 gap-5;
}

.metric-card-top {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl p-4;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
  transition: box-shadow 0.2s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.dark .metric-card-top {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}

.metric-card-clickable {
  cursor: pointer;
}

.metric-card-clickable:hover {
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.08), 0 2px 4px -1px rgba(0, 0, 0, 0.04);
  border-color: rgba(59, 130, 246, 0.4);
}

.dark .metric-card-clickable:hover {
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.5), 0 2px 4px -1px rgba(0, 0, 0, 0.3);
  border-color: rgba(59, 130, 246, 0.5);
}

.metric-card {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/50 dark:border-gray-800/50;
  @apply rounded-lg p-4;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.05);
}

.dark .metric-card {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.3);
}

.metric-label {
  @apply text-[11px] font-bold text-gray-500 dark:text-gray-400 tracking-wider uppercase;
  letter-spacing: 0.05em;
}

.metric-subtext {
  @apply text-[11px] text-gray-500 dark:text-gray-400 font-medium;
}

.metric-subtext-bottom {
  @apply text-[11px] text-gray-500 dark:text-gray-400 font-medium mt-2;
}

.card-arrow {
  transition: transform 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.metric-card-clickable:hover .card-arrow,
.chart-card-clickable:hover .card-arrow {
  transform: translateX(2px);
  color: #3b82f6;
}

.dark .metric-card-clickable:hover .card-arrow,
.dark .chart-card-clickable:hover .card-arrow {
  color: #60a5fa;
}

.metric-value {
  @apply text-2xl font-bold;
  @apply tracking-tight mt-1;
  letter-spacing: -0.025em;
  line-height: 1.2;
}

/* Charts Grid */
.charts-grid {
  @apply grid grid-cols-1 lg:grid-cols-2 gap-5;
}

.chart-card {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl overflow-hidden;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
  transition: box-shadow 0.2s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.dark .chart-card {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}

.chart-card-clickable {
  cursor: pointer;
}

.chart-card-clickable:hover {
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.08), 0 2px 4px -1px rgba(0, 0, 0, 0.04);
  border-color: rgba(59, 130, 246, 0.4);
}

.dark .chart-card-clickable:hover {
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.5), 0 2px 4px -1px rgba(0, 0, 0, 0.3);
  border-color: rgba(59, 130, 246, 0.5);
}

.chart-header {
  @apply px-5 py-4 border-b border-gray-200/60 dark:border-gray-800/60;
  @apply flex items-center justify-between;
  background: transparent;
}

.dark .chart-header {
  background: transparent;
}

.chart-title {
  @apply text-sm font-semibold text-gray-900 dark:text-white tracking-tight;
  letter-spacing: -0.01em;
}

.chart-badge {
  @apply text-[10px] text-gray-600 dark:text-gray-400 bg-gray-100/80 dark:bg-gray-800/80;
  @apply px-2.5 py-1 rounded-full font-semibold tracking-wide;
  border: 1px solid rgba(0, 0, 0, 0.04);
}

.dark .chart-badge {
  border-color: rgba(255, 255, 255, 0.06);
}

.chart-body {
  @apply p-5;
}

/* Error card */
.error-card {
  @apply bg-red-50 dark:bg-red-900/10 border border-red-200/60 dark:border-red-800/60;
  @apply rounded-xl p-4 text-sm text-red-800 dark:text-red-400;
  box-shadow: 0 1px 3px 0 rgba(239, 68, 68, 0.1);
}

/* Table styles inherited from professional.css */
</style>
