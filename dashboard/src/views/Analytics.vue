<template>
  <AppLayout>
    <div class="space-y-6">
      <!-- Filters -->
      <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-4">
        <div class="space-y-4">
          <!-- Row 1: Time Range and Auto-refresh -->
          <div class="flex flex-wrap items-center gap-4">
            <!-- Time Range Quick Select -->
            <div class="flex items-center space-x-2">
              <label class="text-sm font-medium text-gray-700 dark:text-gray-300">
                Time Range:
              </label>
              <div class="flex rounded-lg border border-gray-300 dark:border-gray-700 overflow-hidden">
                <button
                  v-for="range in timeRanges"
                  :key="range.value"
                  @click="selectedTimeRange = range.value; fetchAnalytics()"
                  :class="[
                    'px-4 py-2 text-sm font-medium transition-colors',
                    selectedTimeRange === range.value
                      ? 'bg-primary-600 text-white'
                      : 'bg-white dark:bg-gray-800 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700'
                  ]"
                >
                  {{ range.label }}
                </button>
              </div>
            </div>
            
            <!-- Auto-refresh Toggle -->
            <div class="flex items-center space-x-2">
              <label class="text-sm font-medium text-gray-700 dark:text-gray-300">
                Auto-refresh:
              </label>
              <button
                @click="toggleAutoRefresh"
                :class="[
                  'relative inline-flex h-6 w-11 items-center rounded-full transition-colors',
                  autoRefresh ? 'bg-primary-600' : 'bg-gray-200 dark:bg-gray-700'
                ]"
              >
                <span
                  :class="[
                    'inline-block h-4 w-4 transform rounded-full bg-white transition-transform',
                    autoRefresh ? 'translate-x-6' : 'translate-x-1'
                  ]"
                />
              </button>
            </div>
            
            <!-- Manual Refresh -->
            <button
              @click="fetchAnalytics"
              :disabled="loading"
              class="px-4 py-2 bg-primary-600 hover:bg-primary-700 text-white rounded-lg text-sm font-medium transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
            >
              {{ loading ? 'Loading...' : 'Refresh' }}
            </button>
          </div>
          
          <!-- Row 2: Resource Filters -->
          <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
            <!-- Namespace Filter -->
            <div>
              <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                Namespace
              </label>
              <select
                v-model="namespaceFilter"
                @change="queueFilter = ''; fetchAnalytics()"
                class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent"
              >
                <option value="">All namespaces</option>
                <option v-for="ns in availableNamespaces" :key="ns" :value="ns">
                  {{ ns }}
                </option>
              </select>
            </div>
            
            <!-- Queue Filter -->
            <div>
              <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                Queue
              </label>
              <select
                v-model="queueFilter"
                @change="fetchAnalytics"
                class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent"
              >
                <option value="">All queues</option>
                <option v-for="queue in availableQueues" :key="queue" :value="queue">
                  {{ queue }}
                </option>
              </select>
            </div>
            
            <!-- Task Filter -->
            <div>
              <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                Task
              </label>
              <select
                v-model="taskFilter"
                @change="fetchAnalytics"
                class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent"
              >
                <option value="">All tasks</option>
                <option v-for="task in availableTasks" :key="task" :value="task">
                  {{ task }}
                </option>
              </select>
            </div>
          </div>
          
          <!-- Active Filters Display -->
          <div v-if="hasActiveFilters" class="flex flex-wrap gap-2">
            <span class="text-sm text-gray-600 dark:text-gray-400">Active filters:</span>
            <span v-if="namespaceFilter" class="inline-flex items-center px-3 py-1 rounded-full text-sm bg-primary-100 dark:bg-primary-900/30 text-primary-800 dark:text-primary-300">
              Namespace: {{ namespaceFilter }}
              <button @click="namespaceFilter = ''; fetchAnalytics()" class="ml-2 hover:text-primary-900 dark:hover:text-primary-200">
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
                </svg>
              </button>
            </span>
            <span v-if="queueFilter" class="inline-flex items-center px-3 py-1 rounded-full text-sm bg-primary-100 dark:bg-primary-900/30 text-primary-800 dark:text-primary-300">
              Queue: {{ queueFilter }}
              <button @click="queueFilter = ''; fetchAnalytics()" class="ml-2 hover:text-primary-900 dark:hover:text-primary-200">
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
                </svg>
              </button>
            </span>
            <span v-if="taskFilter" class="inline-flex items-center px-3 py-1 rounded-full text-sm bg-primary-100 dark:bg-primary-900/30 text-primary-800 dark:text-primary-300">
              Task: {{ taskFilter }}
              <button @click="taskFilter = ''; fetchAnalytics()" class="ml-2 hover:text-primary-900 dark:hover:text-primary-200">
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
                </svg>
              </button>
            </span>
            <button
              @click="clearFilters"
              class="text-sm text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white"
            >
              Clear all
            </button>
          </div>
        </div>
      </div>
      
      <LoadingState :loading="loading" :error="error" @retry="fetchAnalytics">
        <div class="space-y-6">
          <!-- Summary Cards -->
        <div class="grid grid-cols-1 md:grid-cols-4 gap-6">
          <MetricCard
            title="Total Throughput"
            :value="(data?.totalThroughput || 0)"
            unit="msg/s"
            icon-color="blue"
          />
          
          <MetricCard
            title="Avg Latency"
            :value="(data?.avgLatency || 0)"
            unit="ms"
            icon-color="green"
          />
          
          <MetricCard
            title="Error Rate"
            :value="(data?.errorRate || 0)"
            unit="%"
            icon-color="red"
          />
          
          <MetricCard
            title="Active Queues"
            :value="(data?.activeQueues || 0)"
            icon-color="purple"
          />
        </div>
        
        <!-- Throughput Chart -->
        <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-primary-500/5 transition-all duration-300">
          <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-6">
            Message Throughput Over Time
          </h2>
          <div class="h-80">
            <Bar v-if="throughputChartData" :data="throughputChartData" :options="chartOptions" />
          </div>
        </div>
        
        <!-- Two Column Layout -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <!-- Error Distribution -->
          <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-red-500/5 transition-all duration-300">
            <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-6">
              Failed Messages Over Time
            </h2>
            <div class="h-64">
              <Bar v-if="errorChartData" :data="errorChartData" :options="chartOptions" />
            </div>
          </div>
          
          <!-- Top Queues by Volume -->
          <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-primary-500/5 transition-all duration-300">
            <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-6">
              Top Queues by Volume
            </h2>
            <div class="space-y-2">
              <div 
                v-for="(queue, index) in topQueues" 
                :key="queue.name"
                class="group flex items-center justify-between p-4 rounded-xl hover:bg-gradient-to-r hover:from-primary-50 hover:to-transparent dark:hover:from-primary-900/10 dark:hover:to-transparent cursor-pointer transition-all border border-transparent hover:border-primary-200 dark:hover:border-primary-800"
                @click="$router.push(`/queues/${queue.name}`)"
              >
                <div class="flex items-center space-x-3">
                  <span class="text-2xl font-bold text-gray-400 dark:text-gray-600">
                    {{ index + 1 }}
                  </span>
                  <div>
                    <div class="font-medium text-gray-900 dark:text-white">
                      {{ queue.name }}
                    </div>
                    <div class="text-sm text-gray-500 dark:text-gray-400">
                      {{ queue.namespace || 'default' }}
                    </div>
                  </div>
                </div>
                <div class="text-right">
                  <div class="text-sm font-medium text-gray-900 dark:text-white">
                    {{ (queue.messagesProcessed || 0).toLocaleString() }}
                  </div>
                  <div class="text-xs text-gray-500 dark:text-gray-400">
                    processed
                  </div>
                </div>
              </div>
              
              <div v-if="!topQueues || topQueues.length === 0" class="text-center py-8 text-gray-500 dark:text-gray-400">
                No data available
              </div>
            </div>
          </div>
        </div>
        
        <!-- Latency Percentiles Chart -->
        <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-purple-500/5 transition-all duration-300">
          <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-6">
            Processing Latency Percentiles
          </h2>
          <div class="h-64">
            <Bar v-if="latencyChartData" :data="latencyChartData" :options="chartOptions" />
          </div>
        </div>
        
        <!-- System Performance Table -->
        <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-primary-500/5 transition-all duration-300">
          <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-6">
            System Performance Metrics
          </h2>
          <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6">
            <div class="p-5 bg-gradient-to-br from-gray-50 to-gray-100/50 dark:from-gray-800 dark:to-gray-800/50 rounded-xl border border-gray-200 dark:border-gray-700">
              <div class="text-sm text-gray-600 dark:text-gray-400 font-medium">Messages Ingested</div>
              <div class="mt-2 text-2xl font-bold text-gray-900 dark:text-white">
                {{ (data?.messagesIngested || 0).toLocaleString() }}
              </div>
            </div>
            
            <div class="p-5 bg-gradient-to-br from-gray-50 to-gray-100/50 dark:from-gray-800 dark:to-gray-800/50 rounded-xl border border-gray-200 dark:border-gray-700">
              <div class="text-sm text-gray-600 dark:text-gray-400 font-medium">Messages Processed</div>
              <div class="mt-2 text-2xl font-bold text-gray-900 dark:text-white">
                {{ (data?.messagesProcessed || 0).toLocaleString() }}
              </div>
            </div>
            
            <div class="p-5 bg-gradient-to-br from-gray-50 to-gray-100/50 dark:from-gray-800 dark:to-gray-800/50 rounded-xl border border-gray-200 dark:border-gray-700">
              <div class="text-sm text-gray-600 dark:text-gray-400 font-medium">Messages Failed</div>
              <div class="mt-2 text-2xl font-bold text-red-600 dark:text-red-400">
                {{ (data?.messagesFailed || 0).toLocaleString() }}
              </div>
            </div>
            
            <div class="p-5 bg-gradient-to-br from-gray-50 to-gray-100/50 dark:from-gray-800 dark:to-gray-800/50 rounded-xl border border-gray-200 dark:border-gray-700">
              <div class="text-sm text-gray-600 dark:text-gray-400 font-medium">Success Rate</div>
              <div class="mt-2 text-2xl font-bold text-primary-600 dark:text-primary-400">
                {{ data?.successRate || 0 }}%
              </div>
            </div>
          </div>
        </div>
      </div>
      </LoadingState>
    </div>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { Bar } from 'vue-chartjs';
import { Chart as ChartJS, CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend } from 'chart.js';
import AppLayout from '../components/layout/AppLayout.vue';
import MetricCard from '../components/common/MetricCard.vue';
import LoadingState from '../components/common/LoadingState.vue';
import { useApi } from '../composables/useApi';
import { usePolling } from '../composables/usePolling';

ChartJS.register(CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend);

const { loading, error, execute, client } = useApi();
const data = ref(null);
const allQueues = ref([]);
const selectedTimeRange = ref('24h');
const autoRefresh = ref(false);
const namespaceFilter = ref('');
const queueFilter = ref('');
const taskFilter = ref('');

const timeRanges = [
  { label: '1h', value: '1h' },
  { label: '6h', value: '6h' },
  { label: '24h', value: '24h' },
  { label: '7d', value: '7d' },
  { label: '30d', value: '30d' }
];

// Get unique namespaces from queues
const availableNamespaces = computed(() => {
  const namespaces = new Set();
  allQueues.value.forEach(q => {
    if (q.namespace) {
      namespaces.add(q.namespace);
    }
  });
  return Array.from(namespaces).sort();
});

// Get unique tasks from queues
const availableTasks = computed(() => {
  const tasks = new Set();
  allQueues.value.forEach(q => {
    if (q.task) {
      tasks.add(q.task);
    }
  });
  return Array.from(tasks).sort();
});

// Get queue names (filtered by namespace if set)
const availableQueues = computed(() => {
  let queues = allQueues.value;
  if (namespaceFilter.value) {
    queues = queues.filter(q => q.namespace === namespaceFilter.value);
  }
  return queues.map(q => q.name).sort();
});

const hasActiveFilters = computed(() => {
  return namespaceFilter.value || queueFilter.value || taskFilter.value;
});

const clearFilters = () => {
  namespaceFilter.value = '';
  queueFilter.value = '';
  taskFilter.value = '';
  fetchAnalytics();
};

// Fetch available queues for filters
const fetchQueuesForFilters = async () => {
  try {
    const result = await client.getQueues({ limit: 1000 });
    allQueues.value = result.queues || [];
  } catch (err) {
    console.error('Failed to fetch queues for filters:', err);
  }
};

const fetchAnalytics = async () => {
  try {
    // Calculate from/to timestamps based on selected range
    const to = new Date();
    let from;
    let interval = 'hour';
    
    switch (selectedTimeRange.value) {
      case '1h':
        from = new Date(to.getTime() - 60 * 60 * 1000);
        interval = 'minute';
        break;
      case '6h':
        from = new Date(to.getTime() - 6 * 60 * 60 * 1000);
        interval = 'hour';
        break;
      case '24h':
        from = new Date(to.getTime() - 24 * 60 * 60 * 1000);
        interval = 'hour';
        break;
      case '7d':
        from = new Date(to.getTime() - 7 * 24 * 60 * 60 * 1000);
        interval = 'day';
        break;
      case '30d':
        from = new Date(to.getTime() - 30 * 24 * 60 * 60 * 1000);
        interval = 'day';
        break;
      default:
        from = new Date(to.getTime() - 24 * 60 * 60 * 1000);
    }
    
    const params = {
      from: from.toISOString(),
      to: to.toISOString(),
      interval
    };
    
    // Add resource filters if set
    if (namespaceFilter.value) {
      params.namespace = namespaceFilter.value;
    }
    if (queueFilter.value) {
      params.queue = queueFilter.value;
    }
    if (taskFilter.value) {
      params.task = taskFilter.value;
    }
    
    const result = await execute(client.getAnalytics.bind(client), params);
    
    // Transform API response to dashboard format
    if (result) {
      data.value = transformAnalyticsData(result);
    } else {
      data.value = getEmptyAnalyticsData();
    }
  } catch (err) {
    console.error('Failed to fetch analytics:', err);
    data.value = getEmptyAnalyticsData();
  }
};

const getEmptyAnalyticsData = () => ({
  totalThroughput: 0,
  avgLatency: 0,
  errorRate: 0,
  activeQueues: 0,
  throughputOverTime: { labels: [], ingested: [], processed: [] },
  errorsByType: { labels: [], counts: [] },
  latencyPercentiles: { p50: 0, p75: 0, p90: 0, p95: 0, p99: 0 },
  topQueues: [],
  messagesIngested: 0,
  messagesProcessed: 0,
  messagesFailed: 0,
  successRate: 0
});

const transformAnalyticsData = (apiData) => {
  // Extract throughput time series
  const throughputSeries = apiData.throughput?.timeSeries || [];
  const interval = apiData.timeRange?.interval || 'hour';
  
  const labels = throughputSeries.map(item => {
    // Parse UTC timestamp and convert to local time
    const date = new Date(item.timestamp);
    
    if (interval === 'minute') {
      return date.toLocaleTimeString([], { 
        hour: '2-digit', 
        minute: '2-digit',
        hour12: false
      });
    } else if (interval === 'hour') {
      return date.toLocaleString([], { 
        month: 'short', 
        day: 'numeric', 
        hour: '2-digit',
        minute: '2-digit',
        hour12: false
      });
    } else {
      return date.toLocaleDateString([], { 
        month: 'short', 
        day: 'numeric'
      });
    }
  }).reverse();
  
  const ingested = throughputSeries.map(item => item.ingested || 0).reverse();
  const processed = throughputSeries.map(item => item.processed || 0).reverse();
  
  // Extract latency data
  const latencyOverall = apiData.latency?.overall || {};
  
  // Extract error rates
  const errorSeries = apiData.errorRates?.timeSeries || [];
  const failedCounts = errorSeries.map(item => item.failed || 0).reverse();
  
  // Calculate metrics
  const totals = apiData.throughput?.totals || {};
  const totalIngested = totals.ingested || 0;
  const totalProcessed = totals.processed || 0;
  const totalFailed = apiData.errorRates?.overall?.failed || 0;
  const successRate = totalProcessed > 0 
    ? ((totalProcessed / (totalProcessed + totalFailed)) * 100).toFixed(2)
    : 0;
  
  return {
    totalThroughput: totals.avgIngestedPerSecond || 0,
    avgLatency: latencyOverall.avg || 0,
    errorRate: parseFloat(apiData.errorRates?.overall?.ratePercent || '0'),
    activeQueues: apiData.topQueues?.length || 0,
    throughputOverTime: {
      labels,
      ingested,
      processed
    },
    errorsByType: {
      labels: labels,
      counts: failedCounts
    },
    latencyPercentiles: {
      p50: latencyOverall.p50 || 0,
      p75: 0, // API doesn't provide p75
      p90: 0, // API doesn't provide p90
      p95: latencyOverall.p95 || 0,
      p99: latencyOverall.p99 || 0
    },
    topQueues: apiData.topQueues || [],
    messagesIngested: totalIngested,
    messagesProcessed: totalProcessed,
    messagesFailed: totalFailed,
    successRate
  };
};

const { startPolling, stopPolling } = usePolling(fetchAnalytics, 10000);

const toggleAutoRefresh = () => {
  autoRefresh.value = !autoRefresh.value;
  if (autoRefresh.value) {
    startPolling();
  } else {
    stopPolling();
  }
};

const topQueues = computed(() => {
  if (!data.value?.topQueues) return [];
  return data.value.topQueues.slice(0, 5);
});

const throughputChartData = computed(() => {
  const timeData = data.value?.throughputOverTime || {};
  const labels = timeData.labels || ['No Data'];
  const ingested = timeData.ingested || [0];
  const processed = timeData.processed || [0];
  
  return {
    labels,
    datasets: [
      {
        label: 'Ingested',
        data: ingested,
        backgroundColor: 'rgba(59, 130, 246, 0.9)',
        borderColor: 'rgb(59, 130, 246)',
        borderWidth: 0,
        borderRadius: 6
      },
      {
        label: 'Processed',
        data: processed,
        backgroundColor: 'rgba(16, 185, 129, 0.9)',
        borderColor: 'rgb(16, 185, 129)',
        borderWidth: 0,
        borderRadius: 6
      }
    ]
  };
});

const errorChartData = computed(() => {
  const errors = data.value?.errorsByType || {};
  const labels = errors.labels || ['No Errors'];
  const counts = errors.counts || [0];
  
  return {
    labels,
    datasets: [
      {
        label: 'Failed Messages',
        data: counts,
        backgroundColor: 'rgba(239, 68, 68, 0.9)',
        borderColor: 'rgb(239, 68, 68)',
        borderWidth: 0,
        borderRadius: 6
      }
    ]
  };
});

const latencyChartData = computed(() => {
  const latency = data.value?.latencyPercentiles || {};
  
  return {
    labels: ['p50', 'p75', 'p90', 'p95', 'p99'],
    datasets: [
      {
        label: 'Latency (ms)',
        data: [
          latency.p50 || 0,
          latency.p75 || 0,
          latency.p90 || 0,
          latency.p95 || 0,
          latency.p99 || 0
        ],
        backgroundColor: 'rgba(139, 92, 246, 0.9)',
        borderColor: 'rgb(139, 92, 246)',
        borderWidth: 0,
        borderRadius: 6
      }
    ]
  };
});

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      display: true,
      position: 'top',
      labels: {
        color: 'rgba(100, 116, 139, 0.8)',
        padding: 16,
        font: {
          size: 13,
          weight: '500'
        },
        usePointStyle: true,
        pointStyle: 'circle'
      }
    },
    tooltip: {
      backgroundColor: 'rgba(17, 24, 39, 0.95)',
      padding: 12,
      titleColor: 'rgba(255, 255, 255, 0.9)',
      bodyColor: 'rgba(255, 255, 255, 0.8)',
      borderColor: 'rgba(16, 185, 129, 0.5)',
      borderWidth: 1,
      cornerRadius: 8
    }
  },
  scales: {
    y: {
      beginAtZero: true,
      grid: {
        color: 'rgba(148, 163, 184, 0.08)',
        lineWidth: 1
      },
      ticks: {
        color: 'rgba(100, 116, 139, 0.7)',
        font: {
          size: 12
        }
      },
      border: {
        display: false
      }
    },
    x: {
      grid: {
        display: false
      },
      ticks: {
        color: 'rgba(100, 116, 139, 0.7)',
        font: {
          size: 11,
          weight: '500'
        },
        maxRotation: 45,
        minRotation: 45
      },
      border: {
        display: false
      }
    }
  }
};

onMounted(async () => {
  await fetchQueuesForFilters();
  await fetchAnalytics();
});

onUnmounted(() => {
  if (autoRefresh.value) {
    stopPolling();
  }
});
</script>

