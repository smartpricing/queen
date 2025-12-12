<template>
  <div class="dashboard-professional dashboard-dense">
    <!-- Dashboard Content -->
    <div class="dashboard-content">
      <LoadingSpinner v-if="loading && !overview" />

      <div v-else-if="error" class="error-card">
        <p><strong>Error loading dashboard:</strong> {{ error }}</p>
      </div>

      <template v-else>
        <!-- System Status Banner -->
        <MaintenanceCard />
        
        <!-- Main Metrics - 2 Column Layout -->
        <div class="main-metrics-grid">
          <!-- 1. System Status Card -->
          <div class="status-card">
            <div class="status-card-header">
              <div class="flex items-center gap-2">
                <div class="status-icon-wrapper">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2.5">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
            </div>
                <h3 class="status-card-title">System Status</h3>
          </div>
              <div :class="getOverallSystemStatus()">
                <svg class="w-5 h-5" fill="currentColor" viewBox="0 0 20 20">
                  <circle cx="10" cy="10" r="5"/>
              </svg>
              </div>
            </div>
            <div class="status-card-body-improved">
              <!-- Time Lag Section -->
              <div class="status-section-improved">
                <div class="status-section-header">
                  <svg class="w-4 h-4 text-gray-400 dark:text-gray-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
                  </svg>
                  <span class="status-section-title">Time Lag</span>
          </div>
                <div class="status-metrics-grid-improved">
                  <div class="status-metric-box">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">AVERAGE</span>
                      <div :class="getLagStatusClass(overview?.lag?.time?.avg)">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
              </svg>
            </div>
                    </div>
                    <div :class="getLagValueClassCompact(overview?.lag?.time?.avg)">
                      {{ formatDuration(overview?.lag?.time?.avg || 0) }}
                    </div>
                    <div class="status-metric-detail">median {{ formatDuration(overview?.lag?.time?.median || 0) }}</div>
          </div>
                  <div class="status-metric-box">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">MAXIMUM</span>
                      <div :class="getLagStatusClass(overview?.lag?.time?.max)">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
              </svg>
            </div>
                    </div>
                    <div :class="getLagValueClassCompact(overview?.lag?.time?.max)">
                      {{ formatDuration(overview?.lag?.time?.max || 0) }}
                    </div>
                    <div class="status-metric-detail">min {{ formatDuration(overview?.lag?.time?.min || 0) }}</div>
                  </div>
                </div>
          </div>

              <!-- Offset Lag Section -->
              <div class="status-section-improved">
                <div class="status-section-header">
                  <svg class="w-4 h-4 text-gray-400 dark:text-gray-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M7 16V4m0 0L3 8m4-4l4 4m6 0v12m0 0l4-4m-4 4l-4-4" />
                  </svg>
                  <span class="status-section-title">Offset Lag</span>
                </div>
                <div class="status-metrics-grid-improved">
                  <div class="status-metric-box">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">AVERAGE</span>
                      <div :class="getOffsetLagStatusClass(overview?.lag?.offset?.avg)">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
                </svg>
              </div>
            </div>
                    <div :class="getOffsetLagValueClassCompact(overview?.lag?.offset?.avg)">
                      {{ formatNumber(overview?.lag?.offset?.avg || 0) }}
                    </div>
                    <div class="status-metric-detail">median {{ formatNumber(overview?.lag?.offset?.median || 0) }} msg</div>
          </div>
                  <div class="status-metric-box">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">MAXIMUM</span>
                      <div :class="getOffsetLagStatusClass(overview?.lag?.offset?.max)">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
                </svg>
              </div>
            </div>
                    <div :class="getOffsetLagValueClassCompact(overview?.lag?.offset?.max)">
                      {{ formatNumber(overview?.lag?.offset?.max || 0) }}
                    </div>
                    <div class="status-metric-detail">min {{ formatNumber(overview?.lag?.offset?.min || 0) }} msg</div>
                  </div>
          </div>
              </div>
            </div>
          </div>

          <!-- 2. Queue Metrics Card -->
          <div class="status-card">
            <div class="status-card-header">
              <div class="flex items-center gap-2">
                <div class="status-icon-wrapper">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2.5">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M4 6h16M4 10h16M4 14h16M4 18h16" />
                </svg>
                </div>
                <h3 class="status-card-title">Queue Metrics</h3>
              </div>
              <button 
                @click="navigateToQueues"
                class="view-all-button"
              >
                View All
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
                </svg>
              </button>
            </div>
            <div class="status-card-body-improved">
              <!-- Queue Overview -->
              <div class="status-section-improved">
                <div class="status-section-header">
                  <svg class="w-4 h-4 text-gray-400 dark:text-gray-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2-2v6a2 2 0 002 2h2a2 2 0 002 2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
                  </svg>
                  <span class="status-section-title">Overview</span>
                </div>
                <div class="queue-overview-grid">
                  <div class="queue-overview-item metric-card-clickable" @click="navigateToQueues">
                    <div class="queue-overview-label">
                      <span>TOTAL</span>
                    </div>
                    <div class="queue-overview-value">{{ formatNumber(overview?.queues || 0) }}</div>
                    <div class="queue-overview-subtext">{{ formatNumber(overview?.partitions || 0) }} partitions</div>
                  </div>
                  <div class="queue-overview-item">
                    <div class="queue-overview-label">
                      <span>MESSAGES</span>
                    </div>
                    <div class="queue-overview-value">{{ formatNumber(overview?.messages?.total || 0) }}</div>
                    <div class="queue-overview-subtext">total messages</div>
          </div>
        </div>
        </div>
        
              <!-- Message States -->
              <div class="status-section-improved">
                <div class="status-section-header">
                  <svg class="w-4 h-4 text-gray-400 dark:text-gray-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2" />
               </svg>
                  <span class="status-section-title">Message States</span>
                </div>
                <div class="status-metrics-grid-improved">
                  <div class="status-metric-box metric-card-clickable" @click="navigateToPending">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">PENDING</span>
                      <div class="text-blue-500">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
                        </svg>
                      </div>
                    </div>
                    <div class="status-metric-value-lg text-blue-600 dark:text-blue-400">
                      {{ formatNumber(calculatedPending) }}
                    </div>
                    <div class="status-metric-detail">{{ formatNumber(overview?.messages?.processing || 0) }} processing</div>
                  </div>
                  <div class="status-metric-box metric-card-clickable" @click="navigateToCompleted">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">COMPLETED</span>
                      <div class="text-green-500">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
                        </svg>
                      </div>
                    </div>
                    <div class="status-metric-value-lg text-green-600 dark:text-green-400">
                      {{ formatNumber(overview?.messages?.completed || 0) }}
                    </div>
                    <div class="status-metric-detail">{{ formatNumber(overview?.messages?.deadLetter || 0) }} in DLQ</div>
                  </div>
                </div>
              </div>
            </div>
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

          <!-- Worker Health Chart (NEW) -->
          <div class="chart-card">
            <div class="chart-header">
              <div class="flex items-center gap-2">
                <h3 class="chart-title">Worker Health</h3>
              </div>
              <span class="chart-badge">Last Hour</span>
            </div>
            <div class="chart-body">
              <WorkerHealthChart :data="status" />
            </div>
          </div>
        </div>

        <!-- Second Charts Row -->
        <div class="charts-grid">
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

          <!-- Batch Efficiency Card (NEW) -->
          <div class="chart-card" v-if="status?.messages?.batchEfficiency">
            <div class="chart-header">
              <h3 class="chart-title">Batch Efficiency</h3>
              <span class="chart-badge">Lifetime</span>
            </div>
            <div class="chart-body flex flex-col items-center justify-center">
              <div class="batch-efficiency-grid">
                <div class="batch-metric">
                  <div class="batch-metric-label">PUSH</div>
                  <div class="batch-metric-value">{{ status.messages.batchEfficiency.push?.toFixed(1) || '0' }}</div>
                  <div class="batch-metric-detail">msgs/request</div>
                </div>
                <div class="batch-metric">
                  <div class="batch-metric-label">POP</div>
                  <div class="batch-metric-value text-indigo-600 dark:text-indigo-400">{{ status.messages.batchEfficiency.pop?.toFixed(1) || '0' }}</div>
                  <div class="batch-metric-detail">msgs/request</div>
                </div>
                <div class="batch-metric">
                  <div class="batch-metric-label">ACK</div>
                  <div class="batch-metric-value text-green-600 dark:text-green-400">{{ status.messages.batchEfficiency.ack?.toFixed(1) || '0' }}</div>
                  <div class="batch-metric-detail">msgs/request</div>
                </div>
              </div>
              <div class="batch-summary" v-if="status?.errors">
                <div class="batch-summary-item" :class="{ 'text-red-500': status.errors.dbErrors > 0 }">
                  <span class="batch-summary-label">DB Errors:</span>
                  <span class="batch-summary-value">{{ formatNumber(status.errors.dbErrors) }}</span>
                </div>
                <div class="batch-summary-item" :class="{ 'text-red-500': status.errors.ackFailed > 0 }">
                  <span class="batch-summary-label">Ack Failed:</span>
                  <span class="batch-summary-value">{{ formatNumber(status.errors.ackFailed) }}</span>
                </div>
                <div class="batch-summary-item" :class="{ 'text-yellow-500': status.errors.dlqMessages > 0 }">
                  <span class="batch-summary-label">DLQ:</span>
                  <span class="batch-summary-value">{{ formatNumber(status.errors.dlqMessages) }}</span>
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- Workers Status Card -->
        <div class="chart-card" v-if="status?.workers?.length">
          <div class="chart-header">
            <div class="flex items-center gap-2">
              <h3 class="chart-title">{{ status.workers.length }} Workers</h3>
            </div>
            <span :class="['health-badge', getOverallWorkerHealthClass(status.workers)]">
              {{ getOverallWorkerHealthStatus(status.workers) }}
            </span>
          </div>
          <div class="chart-body">
            <div class="workers-summary-grid">
              <div class="worker-summary-metric">
                <div class="worker-summary-label">AVG EVENT LOOP</div>
                <div :class="['worker-summary-value', getEventLoopColorClass(getAvgEventLoopLag(status.workers))]">
                  {{ getAvgEventLoopLag(status.workers) }}ms
                </div>
                <div class="worker-summary-detail">max {{ getMaxEventLoopLag(status.workers) }}ms</div>
              </div>
              <div class="worker-summary-metric">
                <div class="worker-summary-label">CONNECTION POOL</div>
                <div :class="['worker-summary-value', getPoolColorClass(getTotalFreeSlots(status.workers), getTotalConnections(status.workers))]">
                  {{ getTotalFreeSlots(status.workers) }}/{{ getTotalConnections(status.workers) }}
                </div>
                <div class="worker-summary-detail">{{ getPoolUtilization(status.workers) }}% utilized</div>
              </div>
              <div class="worker-summary-metric">
                <div class="worker-summary-label">JOB QUEUE</div>
                <div :class="['worker-summary-value', getQueueColorClass(getMaxJobQueue(status.workers))]">
                  {{ getMaxJobQueue(status.workers) }}
                </div>
                <div class="worker-summary-detail">max pending</div>
              </div>
            </div>
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
import { useAutoRefresh } from '../composables/useAutoRefresh';

import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import MaintenanceCard from '../components/MaintenanceCard.vue';
import ThroughputChart from '../components/dashboard/ThroughputChart.vue';
import ResourceUsageChart from '../components/dashboard/ResourceUsageChart.vue';
import WorkerHealthChart from '../components/dashboard/WorkerHealthChart.vue';
import TopQueuesTable from '../components/dashboard/TopQueuesTable.vue';

const router = useRouter();

const loading = ref(false);
const error = ref(null);
const overview = ref(null);
const status = ref(null);
const systemMetrics = ref(null);
const topQueues = ref([]);

// Get latest SharedState from system metrics
const latestSharedState = computed(() => {
  if (!systemMetrics.value?.replicas?.length) return null;
  
  // Get the latest from the first replica
  const firstReplica = systemMetrics.value.replicas[0];
  if (!firstReplica?.timeSeries?.length) return null;
  
  const lastPoint = firstReplica.timeSeries[firstReplica.timeSeries.length - 1];
  return lastPoint?.metrics?.shared_state || null;
});

// Calculate cache hit rate from SharedState
const cacheHitRate = computed(() => {
  const ss = latestSharedState.value;
  if (!ss || !ss.enabled) return null;
  
  const qcHits = ss.queue_config_cache?.hits || 0;
  const qcMisses = ss.queue_config_cache?.misses || 0;
  const pidHits = ss.partition_id_cache?.hits || 0;
  const pidMisses = ss.partition_id_cache?.misses || 0;
  
  const totalHits = qcHits + pidHits;
  const totalMisses = qcMisses + pidMisses;
  const total = totalHits + totalMisses;
  
  return total > 0 ? (totalHits / total * 100).toFixed(1) : '100.0';
});

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

// Format duration in seconds to human-readable string
function formatDuration(seconds) {
  if (!seconds || seconds === 0) return '0s';
  
  if (seconds < 60) return `${seconds}s`;
  if (seconds < 3600) {
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return secs > 0 ? `${mins}m ${secs}s` : `${mins}m`;
  }
  if (seconds < 86400) {
    const hours = Math.floor(seconds / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    return mins > 0 ? `${hours}h ${mins}m` : `${hours}h`;
  }
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  return hours > 0 ? `${days}d ${hours}h` : `${days}d`;
}

// Get overall system status based on lag metrics
function getOverallSystemStatus() {
  const timeLagMax = overview.value?.lag?.time?.max || 0;
  const offsetLagMax = overview.value?.lag?.offset?.max || 0;
  
  // Red if time lag > 5min or offset lag > 50
  if (timeLagMax >= 300 || offsetLagMax >= 50) {
    return 'text-red-500';
  }
  // Yellow if time lag > 1min or offset lag > 10
  if (timeLagMax >= 60 || offsetLagMax >= 10) {
    return 'text-yellow-500';
  }
  // Green otherwise
  return 'text-green-500';
}

// Get lag status indicator class (time-based)
function getLagStatusClass(seconds) {
  if (!seconds || seconds === 0) return 'text-gray-400 dark:text-gray-500';
  if (seconds < 60) return 'text-green-500'; // < 1 min: green
  if (seconds < 300) return 'text-yellow-500'; // 1-5 min: yellow
  return 'text-red-500'; // > 5 min: red
}

// Get lag value class (time-based) - compact version
function getLagValueClassCompact(seconds) {
  if (!seconds || seconds === 0) return 'status-metric-value-lg text-gray-600 dark:text-gray-300';
  if (seconds < 60) return 'status-metric-value-lg text-gray-900 dark:text-gray-100';
  if (seconds < 300) return 'status-metric-value-lg text-yellow-600 dark:text-yellow-400';
  return 'status-metric-value-lg text-red-600 dark:text-red-400';
}

// Get offset lag status indicator class
function getOffsetLagStatusClass(count) {
  if (!count || count === 0) return 'text-gray-400 dark:text-gray-500';
  if (count < 10) return 'text-green-500'; // < 10 msgs: green
  if (count < 50) return 'text-yellow-500'; // 10-50 msgs: yellow
  return 'text-red-500'; // > 50 msgs: red
}

// Get offset lag value class - compact version
function getOffsetLagValueClassCompact(count) {
  if (!count || count === 0) return 'status-metric-value-lg text-gray-600 dark:text-gray-300';
  if (count < 10) return 'status-metric-value-lg text-gray-900 dark:text-gray-100';
  if (count < 50) return 'status-metric-value-lg text-yellow-600 dark:text-yellow-400';
  return 'status-metric-value-lg text-red-600 dark:text-red-400';
}

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

// Set up auto-refresh (every 30 seconds)
const autoRefresh = useAutoRefresh(loadData, {
  interval: 30000, // 30 seconds
  immediate: true, // Load data immediately on mount
  enabled: true, // Auto-refresh enabled by default
});

onMounted(() => {
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

// Worker metrics helpers
function getAvgEventLoopLag(workers) {
  if (!workers?.length) return 0;
  return Math.round(workers.reduce((sum, w) => sum + (w.avgEventLoopLagMs || 0), 0) / workers.length);
}

function getMaxEventLoopLag(workers) {
  if (!workers?.length) return 0;
  return Math.max(...workers.map(w => w.maxEventLoopLagMs || 0));
}

function getTotalFreeSlots(workers) {
  if (!workers?.length) return 0;
  return workers.reduce((sum, w) => sum + (w.freeSlots || 0), 0);
}

function getTotalConnections(workers) {
  if (!workers?.length) return 0;
  return workers.reduce((sum, w) => sum + (w.dbConnections || 0), 0);
}

function getMaxJobQueue(workers) {
  if (!workers?.length) return 0;
  return Math.max(...workers.map(w => w.jobQueueSize || 0));
}

function getPoolUtilization(workers) {
  const free = getTotalFreeSlots(workers);
  const total = getTotalConnections(workers);
  if (!total) return 0;
  return Math.round((1 - free / total) * 100);
}

function getOverallWorkerHealthClass(workers) {
  const maxLag = getMaxEventLoopLag(workers);
  if (maxLag > 1000) return 'health-badge-danger';
  if (maxLag > 100) return 'health-badge-warning';
  return 'health-badge-good';
}

function getOverallWorkerHealthStatus(workers) {
  const maxLag = getMaxEventLoopLag(workers);
  if (maxLag > 1000) return 'Degraded';
  if (maxLag > 100) return 'Warning';
  return 'Healthy';
}

function getEventLoopColorClass(lag) {
  if (!lag || lag < 50) return 'text-green-600 dark:text-green-400';
  if (lag < 500) return 'text-yellow-600 dark:text-yellow-400';
  return 'text-red-600 dark:text-red-400';
}

function getPoolColorClass(free, total) {
  const ratio = free / (total || 1);
  if (ratio > 0.5) return 'text-green-600 dark:text-green-400';
  if (ratio > 0.2) return 'text-yellow-600 dark:text-yellow-400';
  return 'text-red-600 dark:text-red-400';
}

function getQueueColorClass(queueSize) {
  if (!queueSize || queueSize < 10) return 'text-green-600 dark:text-green-400';
  if (queueSize < 50) return 'text-yellow-600 dark:text-yellow-400';
  return 'text-red-600 dark:text-red-400';
}
</script>

<style scoped>
/* Professional Dashboard Design - Condoktur inspired */

.dashboard-professional {
  @apply min-h-screen bg-gray-50 dark:bg-[#0d1117];
  background-image: 
    radial-gradient(at 0% 0%, rgba(255, 107, 0, 0.03) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.03) 0px, transparent 50%);
}

.dark .dashboard-professional {
  background-image: 
    radial-gradient(at 0% 0%, rgba(255, 107, 0, 0.05) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.05) 0px, transparent 50%);
}

.dashboard-content {
  @apply px-6 lg:px-8 py-6 space-y-6;
}

/* Dense layout overrides */
.dashboard-dense .dashboard-content {
  @apply px-2 lg:px-4 py-3 space-y-3;
}

/* Section Headers */
.section-header {
  @apply mt-6 mb-3 first:mt-0;
}

.section-title {
  @apply text-xs font-semibold uppercase tracking-wider text-gray-500 dark:text-gray-400;
  @apply px-1;
}

/* Main Metrics - 2 Column Layout */
.main-metrics-grid {
  @apply grid grid-cols-1 lg:grid-cols-2 gap-5;
}

.dashboard-dense .main-metrics-grid {
  @apply gap-3;
}

/* Status Cards (New Large Cards) */
.status-card {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl overflow-hidden;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
  transition: box-shadow 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.dark .status-card {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}

.dashboard-dense .status-card {
  @apply rounded-lg;
}

.status-card-header {
  @apply px-6 py-4 border-b border-gray-200/60 dark:border-gray-800/60;
  @apply flex items-center justify-between;
  background: linear-gradient(to bottom, rgba(249, 250, 251, 0.5), transparent);
}

.dark .status-card-header {
  background: linear-gradient(to bottom, rgba(22, 27, 34, 0.5), transparent);
}

.dashboard-dense .status-card-header {
  @apply px-3 py-2;
}

.status-icon-wrapper {
  @apply flex items-center justify-center;
  @apply w-8 h-8 rounded-lg;
  @apply bg-gray-100 dark:bg-gray-800/60;
  @apply text-gray-600 dark:text-gray-400;
}

.dashboard-dense .status-icon-wrapper {
  @apply w-6 h-6 rounded-md;
}

.status-card-title {
  @apply text-sm font-semibold text-gray-900 dark:text-white tracking-tight;
  letter-spacing: -0.01em;
}

.dashboard-dense .status-card-title {
  @apply text-xs;
}

.view-all-button {
  @apply text-xs text-gray-500 dark:text-gray-400;
  @apply hover:text-orange-600 dark:hover:text-orange-400;
  @apply flex items-center gap-1 transition-colors;
  @apply font-medium;
}

.dashboard-dense .view-all-button {
  @apply text-[10px];
}

.status-card-body-improved {
  @apply p-6 space-y-6;
}

.dashboard-dense .status-card-body-improved {
  @apply p-3 space-y-3;
}

/* Improved Status Sections */
.status-section-improved {
  @apply space-y-3;
}

.dashboard-dense .status-section-improved {
  @apply space-y-2;
}

.status-section-header {
  @apply flex items-center gap-2 mb-4;
}

.dashboard-dense .status-section-header {
  @apply mb-2;
}

.status-section-title {
  @apply text-xs font-semibold uppercase tracking-wide;
  @apply text-gray-700 dark:text-gray-300;
  letter-spacing: 0.05em;
}

.dashboard-dense .status-section-title {
  @apply text-[10px];
}

.status-metrics-grid-improved {
  @apply grid grid-cols-2 gap-3;
}

.dashboard-dense .status-metrics-grid-improved {
  @apply gap-2;
}

.status-metric-box {
  @apply bg-gray-50/80 dark:bg-gray-900/40;
  @apply border border-gray-200/50 dark:border-gray-800/50;
  @apply rounded-lg p-4;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.dashboard-dense .status-metric-box {
  @apply p-2.5 rounded-md;
}

.status-metric-box.metric-card-clickable:hover {
  @apply bg-gray-100/90 dark:bg-gray-900/60;
  @apply border-orange-300/60 dark:border-orange-600/60;
  @apply shadow-sm;
  cursor: pointer;
  transform: translateY(-1px);
}

.status-metric-label-improved {
  @apply text-[9px] font-bold uppercase tracking-wider;
  @apply text-gray-500 dark:text-gray-500;
  letter-spacing: 0.08em;
}

.dashboard-dense .status-metric-label-improved {
  @apply text-[8px];
}

.status-metric-value-lg {
  @apply text-2xl font-bold tracking-tight mt-2;
  letter-spacing: -0.025em;
  line-height: 1.1;
}

.dashboard-dense .status-metric-value-lg {
  @apply text-lg mt-1;
}

.status-metric-detail {
  @apply text-[10px] text-gray-500 dark:text-gray-500 font-medium mt-1.5;
}

.dashboard-dense .status-metric-detail {
  @apply mt-0.5;
}

/* Queue Overview Grid */
.queue-overview-grid {
  @apply grid grid-cols-2 gap-4;
}

.dashboard-dense .queue-overview-grid {
  @apply gap-2;
}

.queue-overview-item {
  @apply p-4 rounded-lg;
  @apply bg-gradient-to-br from-gray-50 to-gray-100/50;
  @apply dark:from-gray-900/40 dark:to-gray-900/20;
  @apply border border-gray-200/40 dark:border-gray-800/40;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}

.dashboard-dense .queue-overview-item {
  @apply p-2.5 rounded-md;
}

.queue-overview-item.metric-card-clickable:hover {
  @apply shadow-md;
  @apply border-orange-300/60 dark:border-orange-600/60;
  cursor: pointer;
  transform: translateY(-2px);
}

.queue-overview-label {
  @apply text-[9px] font-bold uppercase tracking-wider;
  @apply text-gray-500 dark:text-gray-500 mb-2;
  letter-spacing: 0.08em;
}

.dashboard-dense .queue-overview-label {
  @apply text-[8px] mb-1;
}

.queue-overview-value {
  @apply text-3xl font-bold tracking-tight;
  @apply text-gray-900 dark:text-gray-100;
  letter-spacing: -0.03em;
  line-height: 1;
}

.dashboard-dense .queue-overview-value {
  @apply text-xl;
}

.queue-overview-subtext {
  @apply text-[10px] text-gray-500 dark:text-gray-500 font-medium mt-2;
}

.dashboard-dense .queue-overview-subtext {
  @apply mt-1;
}


/* Metrics Grid (Old - keeping for charts) */
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
  border-color: rgba(255, 107, 0, 0.4);
}

.dark .metric-card-clickable:hover {
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.5), 0 2px 4px -1px rgba(0, 0, 0, 0.3);
  border-color: rgba(255, 107, 0, 0.5);
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
  color: #FF6B00;
}

.dark .metric-card-clickable:hover .card-arrow,
.dark .chart-card-clickable:hover .card-arrow {
  color: #FF4081;
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

.dashboard-dense .charts-grid {
  @apply gap-3;
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

.dashboard-dense .chart-card {
  @apply rounded-lg;
}

.chart-card-clickable {
  cursor: pointer;
}

.chart-card-clickable:hover {
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.08), 0 2px 4px -1px rgba(0, 0, 0, 0.04);
  border-color: rgba(255, 107, 0, 0.4);
}

.dark .chart-card-clickable:hover {
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.5), 0 2px 4px -1px rgba(0, 0, 0, 0.3);
  border-color: rgba(255, 107, 0, 0.5);
}

.chart-header {
  @apply px-5 py-4 border-b border-gray-200/60 dark:border-gray-800/60;
  @apply flex items-center justify-between;
  background: transparent;
}

.dark .chart-header {
  background: transparent;
}

.dashboard-dense .chart-header {
  @apply px-3 py-2;
}

.chart-title {
  @apply text-sm font-semibold text-gray-900 dark:text-white tracking-tight;
  letter-spacing: -0.01em;
}

.dashboard-dense .chart-title {
  @apply text-xs;
}

.chart-badge {
  @apply text-[10px] text-gray-600 dark:text-gray-400 bg-gray-100/80 dark:bg-gray-800/80;
  @apply px-2.5 py-1 rounded-full font-semibold tracking-wide;
  border: 1px solid rgba(0, 0, 0, 0.04);
}

.dark .chart-badge {
  border-color: rgba(255, 255, 255, 0.06);
}

.dashboard-dense .chart-badge {
  @apply text-[9px] px-2 py-0.5;
}

.chart-body {
  @apply p-5;
}

.dashboard-dense .chart-body {
  @apply p-2;
}

/* Error card */
.error-card {
  @apply bg-red-50 dark:bg-red-900/10 border border-red-200/60 dark:border-red-800/60;
  @apply rounded-xl p-4 text-sm text-red-800 dark:text-red-400;
  box-shadow: 0 1px 3px 0 rgba(239, 68, 68, 0.1);
}

/* Health Badge */
.health-badge {
  @apply text-[10px] font-bold px-2.5 py-1 rounded-full uppercase tracking-wide;
}

.health-badge-good {
  @apply bg-green-500/15 text-green-600 dark:text-green-400;
}

.health-badge-warning {
  @apply bg-yellow-500/15 text-yellow-600 dark:text-yellow-400;
}

.health-badge-danger {
  @apply bg-red-500/15 text-red-600 dark:text-red-400;
}

/* Workers Summary Grid */
.workers-summary-grid {
  @apply grid grid-cols-3 gap-4;
}

.dashboard-dense .workers-summary-grid {
  @apply gap-2;
}

.worker-summary-metric {
  @apply text-center p-4 rounded-lg;
  @apply bg-gray-50/80 dark:bg-gray-900/40;
  @apply border border-gray-200/50 dark:border-gray-800/50;
}

.dashboard-dense .worker-summary-metric {
  @apply p-2;
}

.worker-summary-label {
  @apply text-[9px] font-bold uppercase tracking-wider;
  @apply text-gray-500 dark:text-gray-500 mb-2;
  letter-spacing: 0.08em;
}

.worker-summary-value {
  @apply text-2xl font-bold tracking-tight;
  letter-spacing: -0.025em;
}

.dashboard-dense .worker-summary-value {
  @apply text-lg;
}

.worker-summary-detail {
  @apply text-[10px] text-gray-500 dark:text-gray-500 font-medium mt-1;
}

/* Batch Efficiency Grid */
.batch-efficiency-grid {
  @apply grid grid-cols-3 gap-4;
  @apply w-full max-w-lg;
}

.dashboard-dense .batch-efficiency-grid {
  @apply gap-2;
}

.batch-metric {
  @apply text-center p-4 rounded-lg;
  @apply bg-gray-50/80 dark:bg-gray-900/40;
  @apply border border-gray-200/50 dark:border-gray-800/50;
}

.dashboard-dense .batch-metric {
  @apply p-2;
}

.batch-metric-label {
  @apply text-[9px] font-bold uppercase tracking-wider;
  @apply text-gray-500 dark:text-gray-500 mb-2;
  letter-spacing: 0.08em;
}

.batch-metric-value {
  @apply text-2xl font-bold tracking-tight;
  @apply text-gray-900 dark:text-gray-100;
  letter-spacing: -0.025em;
}

.dashboard-dense .batch-metric-value {
  @apply text-lg;
}

.batch-metric-detail {
  @apply text-[10px] text-gray-500 dark:text-gray-500 font-medium mt-1;
}

.batch-summary {
  @apply flex justify-center gap-6 mt-4 pt-4;
  @apply border-t border-gray-200/50 dark:border-gray-800/50;
  @apply w-full max-w-lg;
}

.dashboard-dense .batch-summary {
  @apply gap-4 mt-2 pt-2;
}

.batch-summary-item {
  @apply text-xs font-medium text-gray-600 dark:text-gray-400;
  @apply flex items-center gap-1;
}

.batch-summary-label {
  @apply text-gray-500 dark:text-gray-500;
}

.batch-summary-value {
  @apply font-bold;
}

/* Table styles inherited from professional.css */
</style>
