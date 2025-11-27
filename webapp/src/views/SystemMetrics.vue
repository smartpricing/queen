<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <!-- Header with Time Range Selector -->
        <div class="filter-card">
          <div class="flex flex-col gap-4">
            <div class="flex items-center justify-between">
              <h2 class="text-lg font-semibold text-gray-900 dark:text-white">System Metrics</h2>
              
              <!-- Quick Time Range Selector -->
              <div class="flex items-center gap-2">
                <button
                  v-for="range in timeRanges"
                  :key="range.value"
                  @click="selectQuickRange(range.value)"
                  :class="[
                    'time-range-btn',
                    selectedTimeRange === range.value && !customMode ? 'time-range-active' : 'time-range-inactive'
                  ]"
                >
                  {{ range.label }}
                </button>
                <button
                  @click="toggleCustomMode"
                  :class="[
                    'time-range-btn',
                    customMode ? 'time-range-active' : 'time-range-inactive'
                  ]"
                >
                  Custom
                </button>
              </div>
            </div>

            <!-- Custom Date/Time Range -->
            <div v-if="customMode" class="flex flex-col sm:flex-row items-start sm:items-center gap-3 pt-2 border-t border-gray-200 dark:border-gray-700">
              <div class="flex items-center gap-2 flex-1 w-full sm:w-auto">
                <label class="text-sm font-medium text-gray-700 dark:text-gray-300 whitespace-nowrap">From:</label>
                <DateTimePicker
                  :model-value="customFromISO"
                  @update:model-value="onCustomFromChange"
                  placeholder="Select start date"
                  :show-presets="true"
                />
              </div>
              <div class="flex items-center gap-2 flex-1 w-full sm:w-auto">
                <label class="text-sm font-medium text-gray-700 dark:text-gray-300 whitespace-nowrap">To:</label>
                <DateTimePicker
                  :model-value="customToISO"
                  @update:model-value="onCustomToChange"
                  placeholder="Select end date"
                  :show-presets="true"
                />
              </div>
              <button
                @click="applyCustomRange"
                class="px-4 py-2 bg-orange-500 hover:bg-orange-600 text-white text-sm font-medium rounded-lg transition-colors whitespace-nowrap"
              >
                Apply
              </button>
            </div>
          </div>
        </div>

        <LoadingSpinner v-if="loading && !data" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading metrics:</strong> {{ error }}</p>
        </div>

        <template v-else>
          <!-- Aggregation Type Selector -->
          <div class="info-card-white">
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

          <!-- CPU & Memory Charts (Side by Side) -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-5">
            <!-- CPU Chart -->
            <div class="chart-card-professional">
              <div class="chart-header">
                <h3 class="chart-title">CPU Usage</h3>
              </div>
              <div class="chart-body">
                <DetailedCpuChart :data="data" :aggregation="selectedAggregation" />
              </div>
            </div>

            <!-- Memory Chart -->
            <div class="chart-card-professional">
              <div class="chart-header">
                <h3 class="chart-title">Memory Usage</h3>
              </div>
              <div class="chart-body">
                <DetailedMemoryChart :data="data" :aggregation="selectedAggregation" />
              </div>
            </div>
          </div>

          <!-- Database Metrics Chart -->
          <div class="chart-card-professional">
            <div class="chart-header">
              <h3 class="chart-title">Database Pool</h3>
            </div>
            <div class="chart-body">
              <DetailedDatabaseChart :data="data" :aggregation="selectedAggregation" />
            </div>
          </div>

          <!-- ThreadPool Metrics Chart -->
          <div class="chart-card-professional">
            <div class="chart-header">
              <h3 class="chart-title">Thread Pool</h3>
            </div>
            <div class="chart-body">
              <DetailedThreadPoolChart :data="data" :aggregation="selectedAggregation" />
            </div>
          </div>

          <!-- Registry Metrics Chart -->
          <div class="chart-card-professional">
            <div class="chart-header">
              <h3 class="chart-title">Registries</h3>
            </div>
            <div class="chart-body">
              <DetailedRegistryChart :data="data" :aggregation="selectedAggregation" />
            </div>
          </div>

          <!-- Shared State Panel (UDPSYNC) -->
          <SharedStatePanel />

          <!-- Stats Summary -->
          <div class="grid grid-cols-1 lg:grid-cols-3 gap-5">
            <div class="info-card-white">
              <h3 class="text-xs font-semibold text-gray-600 dark:text-gray-400 mb-3">Current Status</h3>
              <div class="space-y-2">
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Replicas</span>
                  <span class="font-semibold font-mono">{{ data?.replicaCount || 0 }}</span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Time Range</span>
                  <span class="font-semibold font-mono text-xs">{{ formatTimeRange() }}</span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Bucket Size</span>
                  <span class="font-semibold font-mono">{{ formatBucketSize() }}</span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Data Points</span>
                  <span class="font-semibold font-mono">{{ data?.pointCount || 0 }}</span>
                </div>
              </div>
            </div>

            <div class="info-card-white" v-if="lastMetrics">
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
                  <span class="font-semibold font-mono text-gray-900 dark:text-gray-100">
                    {{ lastMetrics.database?.pool_active?.last || 0 }}
                  </span>
                </div>
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Poll Intentions</span>
                  <span class="font-semibold font-mono text-gray-900 dark:text-gray-100">
                    {{ lastMetrics.registries?.poll_intention?.last || 0 }}
                  </span>
                </div>
              </div>
            </div>

            <div class="info-card-white" v-if="lastMetrics">
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
                <div class="flex justify-between text-sm">
                  <span class="text-gray-600 dark:text-gray-400">Poll Intentions (max)</span>
                  <span class="font-semibold font-mono text-gray-900 dark:text-gray-100">
                    {{ lastMetrics.registries?.poll_intention?.max || 0 }}
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
import DateTimePicker from '../components/common/DateTimePicker.vue';
import DetailedCpuChart from '../components/system-metrics/DetailedCpuChart.vue';
import DetailedMemoryChart from '../components/system-metrics/DetailedMemoryChart.vue';
import DetailedDatabaseChart from '../components/system-metrics/DetailedDatabaseChart.vue';
import DetailedThreadPoolChart from '../components/system-metrics/DetailedThreadPoolChart.vue';
import DetailedRegistryChart from '../components/system-metrics/DetailedRegistryChart.vue';
import SharedStatePanel from '../components/system-metrics/SharedStatePanel.vue';

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
const customMode = ref(false);
const customFrom = ref('');
const customTo = ref('');
const customFromISO = ref('');
const customToISO = ref('');

const aggregationTypes = [
  { label: 'Average', value: 'avg' },
  { label: 'Maximum', value: 'max' },
  { label: 'Minimum', value: 'min' },
];

const selectedAggregation = ref('avg');

const lastMetrics = computed(() => {
  if (!data.value?.replicas?.length) return null;
  // Get the last data point from the first replica
  const firstReplica = data.value.replicas[0];
  if (!firstReplica?.timeSeries?.length) return null;
  return firstReplica.timeSeries[firstReplica.timeSeries.length - 1]?.metrics;
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

function formatBucketSize() {
  if (!data.value?.bucketMinutes) return '1 min';
  const minutes = data.value.bucketMinutes;
  if (minutes === 1) return '1 min';
  if (minutes < 60) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  const remainingMinutes = minutes % 60;
  if (remainingMinutes === 0) return `${hours}h`;
  return `${hours}h ${remainingMinutes}m`;
}

function selectQuickRange(minutes) {
  customMode.value = false;
  selectedTimeRange.value = minutes;
  loadData();
}

function toggleCustomMode() {
  customMode.value = !customMode.value;
  if (customMode.value) {
    // Initialize with current range
    const now = new Date();
    const from = new Date(now.getTime() - selectedTimeRange.value * 60 * 1000);
    customTo.value = formatDateTimeLocal(now);
    customFrom.value = formatDateTimeLocal(from);
    customFromISO.value = from.toISOString();
    customToISO.value = now.toISOString();
  }
}

function formatDateTimeLocal(date) {
  // Format: yyyy-MM-ddTHH:mm
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  return `${year}-${month}-${day}T${hours}:${minutes}`;
}

function onCustomFromChange(isoString) {
  customFromISO.value = isoString;
  if (isoString) {
    const date = new Date(isoString);
    customFrom.value = formatDateTimeLocal(date);
  }
}

function onCustomToChange(isoString) {
  customToISO.value = isoString;
  if (isoString) {
    const date = new Date(isoString);
    customTo.value = formatDateTimeLocal(date);
  }
}

function applyCustomRange() {
  if (!customFromISO.value || !customToISO.value) {
    error.value = 'Please select both FROM and TO dates';
    return;
  }
  
  const fromDate = new Date(customFromISO.value);
  const toDate = new Date(customToISO.value);
  
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
    
    if (customMode.value && customFromISO.value && customToISO.value) {
      // Use custom range
      from = new Date(customFromISO.value);
      to = new Date(customToISO.value);
    } else {
      // Use quick range
      const now = new Date();
      from = new Date(now.getTime() - selectedTimeRange.value * 60 * 1000);
      to = now;
    }

    const response = await systemMetricsApi.getSystemMetrics({
      from: from.toISOString(),
      to: to.toISOString(),
    });

    data.value = response.data;
  } catch (err) {
    error.value = err.message;
    console.error('System metrics error:', err);
  } finally {
    loading.value = false;
  }
}

// Watch for time range changes (only when not in custom mode)
watch(selectedTimeRange, () => {
  if (!customMode.value) {
    loadData();
  }
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
/* Styles inherited from professional.css */

.time-range-btn {
  padding: 0.5rem 1rem;
  border-radius: 0.5rem;
  font-size: 0.875rem;
  font-weight: 500;
  transition: all 0.2s ease;
  cursor: pointer;
}

.time-range-active {
  background: rgba(255, 107, 0, 0.1);
  color: #FF6B00;
  border: 1px solid rgba(255, 107, 0, 0.2);
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
  background: rgba(255, 107, 0, 0.15);
}

.time-range-inactive:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .time-range-inactive:hover {
  background: rgba(255, 255, 255, 0.05);
}

.agg-type-btn {
  padding: 0.5rem 0.75rem;
  border-radius: 0.5rem;
  font-size: 0.6875rem;
  font-weight: 600;
  transition: background-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), color 0.15s cubic-bezier(0.4, 0, 0.2, 1);
  cursor: pointer;
  letter-spacing: 0.01em;
}

.agg-type-active {
  background: rgba(255, 107, 0, 0.12);
  color: #2563eb;
  border: 1px solid rgba(255, 107, 0, 0.3);
  box-shadow: 0 1px 2px 0 rgba(255, 107, 0, 0.1);
}

.dark .agg-type-active {
  background: rgba(255, 107, 0, 0.18);
  color: #FF4081;
  border: 1px solid rgba(255, 107, 0, 0.4);
}

.agg-type-inactive {
  background: transparent;
  color: #6b7280;
  border: 1px solid rgba(0, 0, 0, 0.12);
}

.dark .agg-type-inactive {
  color: #9ca3af;
  border-color: rgba(255, 255, 255, 0.12);
}

.agg-type-active:hover {
  background: rgba(255, 107, 0, 0.15);
}

.dark .agg-type-active:hover {
  background: rgba(255, 107, 0, 0.2);
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

.datetime-input {
  padding: 0.5rem 0.75rem;
  border-radius: 0.5rem;
  border: 1px solid rgba(0, 0, 0, 0.1);
  font-size: 0.875rem;
  font-family: ui-monospace, monospace;
  background: white;
  color: #374151;
  transition: border-color 0.2s ease;
}

.datetime-input:focus {
  outline: none;
  border-color: #FF6B00;
  box-shadow: 0 0 0 3px rgba(255, 107, 0, 0.1);
}

.dark .datetime-input {
  background: rgba(31, 41, 55, 0.5);
  color: #e5e7eb;
  border-color: rgba(255, 255, 255, 0.1);
}

.dark .datetime-input:focus {
  border-color: #FF6B00;
  box-shadow: 0 0 0 3px rgba(255, 107, 0, 0.2);
}
</style>

