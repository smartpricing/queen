<template>
  <div class="system-metrics-page">
    <div class="py-4 px-3">
      <div class="space-y-2.5">
        <!-- Header -->
        <div class="flex items-center justify-between mb-4">
          <div class="flex items-center gap-3">
            <div class="w-10 h-10 rounded-xl bg-blue-500/10 dark:bg-blue-500/20 flex items-center justify-center">
              <svg class="w-6 h-6 text-blue-600 dark:text-blue-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
              </svg>
            </div>
            <div>
              <h1 class="text-xl font-bold text-gray-900 dark:text-white">System Metrics</h1>
              <p class="text-sm text-gray-500 dark:text-gray-400">Real-time system performance monitoring</p>
            </div>
          </div>

          <!-- Time Range Selector -->
          <div class="flex items-center gap-2">
            <button
              v-for="range in timeRanges"
              :key="range.value"
              @click="selectedTimeRange = range.value"
              :class="[
                'time-range-btn',
                selectedTimeRange === range.value ? 'time-range-active' : 'time-range-inactive'
              ]"
            >
              {{ range.label }}
            </button>
          </div>
        </div>

        <LoadingSpinner v-if="loading && !data" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading metrics:</strong> {{ error }}</p>
        </div>

        <template v-else>
          <!-- Aggregation Type Selector -->
          <div class="chart-flat">
            <div class="flex items-center justify-between">
              <div class="flex items-center gap-2">
                <svg class="w-5 h-5 text-gray-600 dark:text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 6V4m0 2a2 2 0 100 4m0-4a2 2 0 110 4m-6 8a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4m6 6v10m6-2a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4" />
                </svg>
                <span class="text-sm font-semibold text-gray-700 dark:text-gray-300">Aggregation Type</span>
              </div>
              <div class="flex items-center gap-2">
                <button
                  v-for="agg in aggregationTypes"
                  :key="agg.value"
                  @click="selectedAggregation = agg.value"
                  :class="[
                    'agg-type-btn',
                    selectedAggregation === agg.value ? 'agg-type-active' : 'agg-type-inactive'
                  ]"
                >
                  {{ agg.label }}
                </button>
              </div>
            </div>
          </div>

          <!-- CPU & Memory Chart -->
          <div class="chart-flat">
            <div class="flex items-center gap-2 mb-4">
              <div class="w-8 h-8 rounded-lg bg-rose-500/10 dark:bg-rose-500/20 flex items-center justify-center">
                <svg class="w-5 h-5 text-rose-600 dark:text-rose-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z" />
                </svg>
              </div>
              <h3 class="text-sm font-semibold text-gray-900 dark:text-gray-100">CPU & Memory Usage</h3>
            </div>
            <DetailedResourceChart :data="data" :aggregation="selectedAggregation" />
          </div>

          <!-- Database Metrics Chart -->
          <div class="chart-flat">
            <div class="flex items-center gap-2 mb-4">
              <div class="w-8 h-8 rounded-lg bg-blue-500/10 dark:bg-blue-500/20 flex items-center justify-center">
                <svg class="w-5 h-5 text-blue-600 dark:text-blue-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4m0 5c0 2.21-3.582 4-8 4s-8-1.79-8-4" />
                </svg>
              </div>
              <h3 class="text-sm font-semibold text-gray-900 dark:text-gray-100">Database Pool</h3>
            </div>
            <DetailedDatabaseChart :data="data" :aggregation="selectedAggregation" />
          </div>

          <!-- ThreadPool Metrics Chart -->
          <div class="chart-flat">
            <div class="flex items-center gap-2 mb-4">
              <div class="w-8 h-8 rounded-lg bg-amber-500/10 dark:bg-amber-500/20 flex items-center justify-center">
                <svg class="w-5 h-5 text-amber-600 dark:text-amber-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2V6zM14 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2V6zM4 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2v-2zM14 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2v-2z" />
                </svg>
              </div>
              <h3 class="text-sm font-semibold text-gray-900 dark:text-gray-100">ThreadPool Metrics</h3>
            </div>
            <DetailedThreadPoolChart :data="data" :aggregation="selectedAggregation" />
          </div>

          <!-- Stats Summary -->
          <div class="grid grid-cols-1 lg:grid-cols-3 gap-2">
            <div class="info-flat">
              <h3 class="text-xs font-semibold text-gray-600 dark:text-gray-400 mb-3">Current Status</h3>
              <div class="space-y-2">
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Data Points</span>
                  <span class="font-semibold font-mono">{{ data?.count || 0 }}</span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Time Range</span>
                  <span class="font-semibold font-mono text-xs">{{ formatTimeRange() }}</span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Sample Interval</span>
                  <span class="font-semibold font-mono">60 seconds</span>
                </div>
              </div>
            </div>

            <div class="info-flat" v-if="lastMetrics">
              <h3 class="text-xs font-semibold text-gray-600 dark:text-gray-400 mb-3">Latest Values</h3>
              <div class="space-y-2">
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">CPU %</span>
                  <span class="font-semibold font-mono text-rose-600 dark:text-rose-400">
                    {{ formatCPU(lastMetrics.cpu?.user_us?.last) }}
                  </span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Memory</span>
                  <span class="font-semibold font-mono text-purple-600 dark:text-purple-400">
                    {{ formatMemory(lastMetrics.memory?.rss_bytes?.last) }}
                  </span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">DB Active</span>
                  <span class="font-semibold font-mono text-blue-600 dark:text-blue-400">
                    {{ lastMetrics.database?.pool_active?.last || 0 }}
                  </span>
                </div>
              </div>
            </div>

            <div class="info-flat" v-if="lastMetrics">
              <h3 class="text-xs font-semibold text-gray-600 dark:text-gray-400 mb-3">Peak Values</h3>
              <div class="space-y-2">
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">CPU % (max)</span>
                  <span class="font-semibold font-mono text-rose-600 dark:text-rose-400">
                    {{ formatCPU(lastMetrics.cpu?.user_us?.max) }}
                  </span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Memory (max)</span>
                  <span class="font-semibold font-mono text-purple-600 dark:text-purple-400">
                    {{ formatMemory(lastMetrics.memory?.rss_bytes?.max) }}
                  </span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">DB Queue (max)</span>
                  <span class="font-semibold font-mono text-amber-600 dark:text-amber-400">
                    {{ lastMetrics.threadpool?.db?.queue_size?.max || 0 }}
                  </span>
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
import { ref, computed, onMounted, onUnmounted, watch } from 'vue';
import { systemMetricsApi } from '../api/system-metrics';

import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import DetailedResourceChart from '../components/system-metrics/DetailedResourceChart.vue';
import DetailedDatabaseChart from '../components/system-metrics/DetailedDatabaseChart.vue';
import DetailedThreadPoolChart from '../components/system-metrics/DetailedThreadPoolChart.vue';

const loading = ref(false);
const error = ref(null);
const data = ref(null);

const timeRanges = [
  { label: '15m', value: 15 },
  { label: '1h', value: 60 },
  { label: '6h', value: 360 },
  { label: '24h', value: 1440 },
];

const selectedTimeRange = ref(60); // Default 1 hour

const aggregationTypes = [
  { label: 'Average', value: 'avg' },
  { label: 'Maximum', value: 'max' },
  { label: 'Minimum', value: 'min' },
];

const selectedAggregation = ref('avg');

const lastMetrics = computed(() => {
  if (!data.value?.timeSeries?.length) return null;
  return data.value.timeSeries[data.value.timeSeries.length - 1]?.metrics;
});

function formatTimeRange() {
  if (!data.value?.timeRange) return 'N/A';
  const from = new Date(data.value.timeRange.from);
  const to = new Date(data.value.timeRange.to);
  return `${from.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })} - ${to.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })}`;
}

function formatCPU(value) {
  if (value === undefined || value === null) return '0%';
  return ((value / 100).toFixed(1)) + '%';
}

function formatMemory(value) {
  if (value === undefined || value === null) return '0 MB';
  return Math.round(value / 1024 / 1024) + ' MB';
}

async function loadData() {
  loading.value = true;
  error.value = null;

  try {
    // Calculate time range
    const now = new Date();
    const from = new Date(now.getTime() - selectedTimeRange.value * 60 * 1000);

    const response = await systemMetricsApi.getSystemMetrics({
      from: from.toISOString(),
      to: now.toISOString(),
    });

    data.value = response.data;
  } catch (err) {
    error.value = err.message;
    console.error('System metrics error:', err);
  } finally {
    loading.value = false;
  }
}

// Watch for time range changes
watch(selectedTimeRange, () => {
  loadData();
});

onMounted(() => {
  loadData();
  
  // Register refresh callback
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/system-metrics', loadData);
  }
});

onUnmounted(() => {
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/system-metrics', null);
  }
});
</script>

<style scoped>
.system-metrics-page {
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
  background: #101319;
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

.time-range-btn {
  padding: 0.5rem 1rem;
  border-radius: 0.5rem;
  font-size: 0.875rem;
  font-weight: 500;
  transition: all 0.2s ease;
  cursor: pointer;
}

.time-range-active {
  background: rgba(59, 130, 246, 0.1);
  color: #3b82f6;
  border: 1px solid rgba(59, 130, 246, 0.2);
}

.time-range-inactive {
  background: transparent;
  color: #9ca3af;
  border: 1px solid rgba(0, 0, 0, 0.1);
}

.dark .time-range-inactive {
  border-color: rgba(255, 255, 255, 0.1);
}

.time-range-btn:hover {
  transform: translateY(-1px);
}

.time-range-active:hover {
  background: rgba(59, 130, 246, 0.15);
}

.time-range-inactive:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .time-range-inactive:hover {
  background: rgba(255, 255, 255, 0.05);
}

.agg-type-btn {
  padding: 0.375rem 0.75rem;
  border-radius: 0.5rem;
  font-size: 0.75rem;
  font-weight: 500;
  transition: all 0.2s ease;
  cursor: pointer;
}

.agg-type-active {
  background: rgba(244, 63, 94, 0.1);
  color: #f43f5e;
  border: 1px solid rgba(244, 63, 94, 0.2);
}

.agg-type-inactive {
  background: transparent;
  color: #9ca3af;
  border: 1px solid rgba(0, 0, 0, 0.1);
}

.dark .agg-type-inactive {
  border-color: rgba(255, 255, 255, 0.1);
}

.agg-type-btn:hover {
  transform: translateY(-1px);
}

.agg-type-active:hover {
  background: rgba(244, 63, 94, 0.15);
}

.agg-type-inactive:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .agg-type-inactive:hover {
  background: rgba(255, 255, 255, 0.05);
}

.error-card {
  background: transparent;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  color: #dc2626;
  font-size: 0.875rem;
}

.dark .error-card {
  color: #fca5a5;
}
</style>

