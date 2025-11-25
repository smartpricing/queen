<template>
  <div class="dashboard-professional">
    <!-- Dashboard Content -->
    <div class="dashboard-content">
      <LoadingSpinner v-if="loading && !overview" />

      <div v-else-if="error" class="error-card">
        <p><strong>Error loading dashboard:</strong> {{ error }}</p>
      </div>

      <template v-else>
        <!-- System Status Banner -->
        <MaintenanceCard />
        
        <!-- Main Metrics - 3 Column Layout -->
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

          <!-- 3. Streaming Metrics Card -->
          <div class="status-card">
            <div class="status-card-header">
              <div class="flex items-center gap-2">
                <div class="status-icon-wrapper">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2.5">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z" />
                  </svg>
                </div>
                <h3 class="status-card-title">Streaming</h3>
              </div>
              <button 
                @click="navigateToStreams"
                class="view-all-button"
              >
                View All
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M9 5l7 7-7 7" />
              </svg>
              </button>
            </div>
            <div class="status-card-body-improved">
              <!-- Stream Overview -->
              <div class="status-section-improved">
                <div class="status-section-header">
                  <svg class="w-4 h-4 text-gray-400 dark:text-gray-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10" />
                  </svg>
                  <span class="status-section-title">Streams</span>
                </div>
                <div class="status-metrics-grid-improved">
                  <div class="status-metric-box metric-card-clickable" @click="navigateToStreams">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">TOTAL</span>
                    </div>
                    <div class="status-metric-value-lg text-purple-600 dark:text-purple-400">
                      {{ formatNumber(streamStats?.totalStreams || 0) }}
                    </div>
                    <div class="status-metric-detail">{{ formatNumber(streamStats?.partitionedStreams || 0) }} partitioned</div>
                  </div>
                  <div class="status-metric-box metric-card-clickable" @click="navigateToStreams">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">CONSUMER GROUPS</span>
                    </div>
                    <div class="status-metric-value-lg text-teal-600 dark:text-teal-400">
                      {{ formatNumber(streamStats?.totalConsumerGroups || 0) }}
                    </div>
                    <div class="status-metric-detail">{{ formatNumber(streamStats?.activeConsumers || 0) }} active consumers</div>
                  </div>
                </div>
          </div>

              <!-- Window Processing -->
              <div class="status-section-improved">
                <div class="status-section-header">
                  <svg class="w-4 h-4 text-gray-400 dark:text-gray-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M9 17V7m0 10a2 2 0 01-2 2H5a2 2 0 01-2-2V7a2 2 0 012-2h2a2 2 0 012 2m0 10a2 2 0 002 2h2a2 2 0 002-2M9 7a2 2 0 012-2h2a2 2 0 012 2m0 10V7m0 10a2 2 0 002 2h2a2 2 0 002-2V7a2 2 0 00-2-2h-2a2 2 0 00-2 2" />
                  </svg>
                  <span class="status-section-title">Windows</span>
                </div>
                <div class="status-metrics-grid-improved">
                  <div class="status-metric-box">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">PROCESSED</span>
              <div class="text-green-500">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
                </svg>
              </div>
            </div>
                    <div class="status-metric-value-lg text-gray-900 dark:text-gray-100">
                      {{ formatNumber(streamStats?.totalWindowsProcessed || 0) }}
                    </div>
                    <div class="status-metric-detail">{{ formatNumber(streamStats?.windowsLastHour || 0) }} last hour</div>
          </div>
                  <div class="status-metric-box">
                    <div class="flex items-center justify-between mb-2">
                      <span class="status-metric-label-improved">ACTIVE</span>
              <div :class="streamStats?.activeLeases > 0 ? 'text-green-500' : 'text-gray-400 dark:text-gray-500'">
                        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 20 20">
                          <circle cx="10" cy="10" r="5"/>
                </svg>
                      </div>
                    </div>
                    <div class="status-metric-value-lg text-gray-900 dark:text-gray-100">
                      {{ formatNumber(streamStats?.activeLeases || 0) }}
                    </div>
                    <div class="status-metric-detail">{{ formatNumber(streamStats?.avgLeaseTime || 0) }}s avg time</div>
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
import { streamsApi } from '../api/streams';
import { formatNumber } from '../utils/formatters';
import { useAutoRefresh } from '../composables/useAutoRefresh';

import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import MaintenanceCard from '../components/MaintenanceCard.vue';
import ThroughputChart from '../components/dashboard/ThroughputChart.vue';
import ResourceUsageChart from '../components/dashboard/ResourceUsageChart.vue';
import TopQueuesTable from '../components/dashboard/TopQueuesTable.vue';

const router = useRouter();

const loading = ref(false);
const error = ref(null);
const overview = ref(null);
const status = ref(null);
const systemMetrics = ref(null);
const topQueues = ref([]);
const streamStats = ref(null);

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
    const [overviewRes, statusRes, queuesRes, systemMetricsRes, streamStatsRes] = await Promise.all([
      resourcesApi.getOverview(),
      analyticsApi.getStatus(),
      queuesApi.getQueues(),
      systemMetricsApi.getSystemMetrics(), // Fetch last hour of system metrics
      streamsApi.getStreamStats().catch(() => ({ data: { totalStreams: 0, activeLeases: 0 } })), // Gracefully handle if streams not supported
    ]);

    overview.value = overviewRes.data;
    status.value = statusRes.data;
    systemMetrics.value = systemMetricsRes.data;
    streamStats.value = streamStatsRes.data;
    
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

function navigateToStreams() {
  router.push('/streams');
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

/* Section Headers */
.section-header {
  @apply mt-6 mb-3 first:mt-0;
}

.section-title {
  @apply text-xs font-semibold uppercase tracking-wider text-gray-500 dark:text-gray-400;
  @apply px-1;
}

/* Main Metrics - 3 Column Layout */
.main-metrics-grid {
  @apply grid grid-cols-1 lg:grid-cols-3 gap-5;
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

.status-card-header {
  @apply px-6 py-4 border-b border-gray-200/60 dark:border-gray-800/60;
  @apply flex items-center justify-between;
  background: linear-gradient(to bottom, rgba(249, 250, 251, 0.5), transparent);
}

.dark .status-card-header {
  background: linear-gradient(to bottom, rgba(22, 27, 34, 0.5), transparent);
}

.status-icon-wrapper {
  @apply flex items-center justify-center;
  @apply w-8 h-8 rounded-lg;
  @apply bg-gray-100 dark:bg-gray-800/60;
  @apply text-gray-600 dark:text-gray-400;
}

.status-card-title {
  @apply text-sm font-semibold text-gray-900 dark:text-white tracking-tight;
  letter-spacing: -0.01em;
}

.view-all-button {
  @apply text-xs text-gray-500 dark:text-gray-400;
  @apply hover:text-orange-600 dark:hover:text-orange-400;
  @apply flex items-center gap-1 transition-colors;
  @apply font-medium;
}

.status-card-body-improved {
  @apply p-6 space-y-6;
}

/* Improved Status Sections */
.status-section-improved {
  @apply space-y-3;
}

.status-section-header {
  @apply flex items-center gap-2 mb-4;
}

.status-section-title {
  @apply text-xs font-semibold uppercase tracking-wide;
  @apply text-gray-700 dark:text-gray-300;
  letter-spacing: 0.05em;
}

.status-metrics-grid-improved {
  @apply grid grid-cols-2 gap-3;
}

.status-metric-box {
  @apply bg-gray-50/80 dark:bg-gray-900/40;
  @apply border border-gray-200/50 dark:border-gray-800/50;
  @apply rounded-lg p-4;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
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

.status-metric-value-lg {
  @apply text-2xl font-bold tracking-tight mt-2;
  letter-spacing: -0.025em;
  line-height: 1.1;
}

.status-metric-detail {
  @apply text-[10px] text-gray-500 dark:text-gray-500 font-medium mt-1.5;
}

/* Queue Overview Grid */
.queue-overview-grid {
  @apply grid grid-cols-2 gap-4;
}

.queue-overview-item {
  @apply text-center p-4 rounded-lg;
  @apply bg-gradient-to-br from-gray-50 to-gray-100/50;
  @apply dark:from-gray-900/40 dark:to-gray-900/20;
  @apply border border-gray-200/40 dark:border-gray-800/40;
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
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

.queue-overview-value {
  @apply text-3xl font-bold tracking-tight;
  @apply text-gray-900 dark:text-gray-100;
  letter-spacing: -0.03em;
  line-height: 1;
}

.queue-overview-subtext {
  @apply text-[10px] text-gray-500 dark:text-gray-500 font-medium mt-2;
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
