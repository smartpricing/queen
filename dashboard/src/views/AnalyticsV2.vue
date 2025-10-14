<template>
  <AppLayout>
    <div class="space-y-6">
      <!-- Header -->
      <div class="flex items-center justify-between">
        <div>
          <h1 class="text-3xl font-black text-gray-900 dark:text-white mb-1">
            Analytics
          </h1>
          <p class="text-sm text-gray-600 dark:text-gray-400">
            Monitor performance and trends
          </p>
        </div>
        
        <QuickActions :actions="quickActions" />
      </div>
      
      <!-- Time Range & Filters -->
      <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
        <div class="space-y-6">
          <!-- Time Range Selection -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-3">
              Time Range
            </label>
            <div class="flex flex-wrap gap-2">
              <button
                v-for="range in timeRanges"
                :key="range.value"
                @click="selectedTimeRange = range.value; fetchAnalytics()"
                :class="[
                  'px-5 py-2.5 rounded-xl text-sm font-bold',
                  selectedTimeRange === range.value
                    ? 'bg-gradient-to-r from-primary-600 to-primary-700 text-white shadow-lg shadow-primary-500/30'
                    : 'bg-white dark:bg-gray-800 text-gray-700 dark:text-gray-300 border border-gray-300 dark:border-gray-700 hover:border-primary-500 dark:hover:border-primary-500'
                ]"
              >
                {{ range.label }}
              </button>
            </div>
          </div>
          
          <!-- Resource Filters -->
          <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
            <!-- Namespace Filter -->
            <div>
              <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-2">
                Namespace
              </label>
              <select
                v-model="namespaceFilter"
                @change="queueFilter = ''; fetchAnalytics()"
                class="w-full px-4 py-2.5 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white font-medium focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
              >
                <option value="">All namespaces</option>
                <option v-for="ns in availableNamespaces" :key="ns" :value="ns">
                  {{ ns }}
                </option>
              </select>
            </div>
            
            <!-- Queue Filter -->
            <div>
              <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-2">
                Queue
              </label>
              <select
                v-model="queueFilter"
                @change="fetchAnalytics"
                class="w-full px-4 py-2.5 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white font-medium focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
              >
                <option value="">All queues</option>
                <option v-for="queue in availableQueues" :key="queue" :value="queue">
                  {{ queue }}
                </option>
              </select>
            </div>
            
            <!-- Task Filter -->
            <div>
              <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-2">
                Task
              </label>
              <select
                v-model="taskFilter"
                @change="fetchAnalytics"
                class="w-full px-4 py-2.5 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white font-medium focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
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
            <span class="text-sm font-medium text-gray-600 dark:text-gray-400">Active filters:</span>
            <span v-if="namespaceFilter" class="inline-flex items-center gap-2 px-3 py-1 rounded-full text-sm font-semibold bg-primary-100 dark:bg-primary-900/30 text-primary-800 dark:text-primary-300">
              Namespace: {{ namespaceFilter }}
              <button @click="namespaceFilter = ''; fetchAnalytics()" class="hover:text-primary-900 dark:hover:text-primary-200">
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M6 18L18 6M6 6l12 12" />
                </svg>
              </button>
            </span>
            <span v-if="queueFilter" class="inline-flex items-center gap-2 px-3 py-1 rounded-full text-sm font-semibold bg-primary-100 dark:bg-primary-900/30 text-primary-800 dark:text-primary-300">
              Queue: {{ queueFilter }}
              <button @click="queueFilter = ''; fetchAnalytics()" class="hover:text-primary-900 dark:hover:text-primary-200">
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M6 18L18 6M6 6l12 12" />
                </svg>
              </button>
            </span>
            <span v-if="taskFilter" class="inline-flex items-center gap-2 px-3 py-1 rounded-full text-sm font-semibold bg-primary-100 dark:bg-primary-900/30 text-primary-800 dark:text-primary-300">
              Task: {{ taskFilter }}
              <button @click="taskFilter = ''; fetchAnalytics()" class="hover:text-primary-900 dark:hover:text-primary-200">
                <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M6 18L18 6M6 6l12 12" />
                </svg>
              </button>
            </span>
            <button
              @click="clearFilters"
              class="text-sm font-semibold text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white"
            >
              Clear all
            </button>
          </div>
        </div>
      </div>
      
      <LoadingState :loading="loading" :error="error" @retry="fetchAnalytics">
        <div class="space-y-6">
          <!-- Summary Metrics -->
          <div class="grid grid-cols-1 md:grid-cols-4 gap-6">
            <MetricCardV2
              title="Total Throughput"
              :value="data?.totalThroughput || 0"
              unit="msg/s"
              icon-color="blue"
              :sparkline-data="generateSparkline('throughput')"
            />
            
            <MetricCardV2
              title="Avg Latency"
              :value="data?.avgLatency || 0"
              unit="ms"
              icon-color="green"
              :sparkline-data="generateSparkline('latency')"
            />
            
            <MetricCardV2
              title="Error Rate"
              :value="data?.errorRate || 0"
              unit="%"
              icon-color="red"
              :sparkline-data="generateSparkline('errors')"
            />
            
            <MetricCardV2
              title="Active Queues"
              :value="data?.activeQueues || 0"
              icon-color="purple"
              :sparkline-data="generateSparkline('queues')"
            />
          </div>
          
          <!-- Throughput Chart -->
          <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
            <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6">
              Message Throughput Over Time
            </h2>
            <div class="h-80">
              <Bar v-if="throughputChartData" :data="throughputChartData" :options="chartOptions" />
            </div>
          </div>
          
          <!-- Two Column Layout -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
            <!-- Error Distribution -->
            <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
              <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6">
                Failed Messages Over Time
              </h2>
              <div class="h-64">
                <Bar v-if="errorChartData" :data="errorChartData" :options="chartOptions" />
              </div>
            </div>
            
            <!-- Top Queues by Volume -->
            <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
              <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6">
                Top Queues by Volume
              </h2>
              <div class="space-y-2">
                <div 
                  v-for="(queue, index) in topQueues" 
                  :key="queue.name"
                  class="group flex items-center justify-between p-4 rounded-xl hover:bg-primary-50/50 dark:hover:bg-primary-900/10 cursor-pointer border border-transparent hover:border-primary-200/50 dark:hover:border-primary-800/50"
                  @click="$router.push(`/queues/${queue.name}`)"
                >
                  <div class="flex items-center gap-4">
                    <div class="flex-shrink-0 w-10 h-10 rounded-xl bg-gradient-to-br from-primary-500 to-blue-600 flex items-center justify-center text-white font-black text-lg shadow-lg shadow-primary-500/30">
                      {{ index + 1 }}
                    </div>
                    <div>
                      <div class="font-bold text-gray-900 dark:text-white">
                        {{ queue.name }}
                      </div>
                      <div class="text-sm text-gray-500 dark:text-gray-400">
                        {{ queue.namespace || 'default' }}
                      </div>
                    </div>
                  </div>
                  <div class="text-right">
                    <div class="text-sm font-black text-gray-900 dark:text-white">
                      {{ formatNumber(queue.messagesProcessed || 0) }}
                    </div>
                    <div class="text-xs text-gray-500 dark:text-gray-400">
                      processed
                    </div>
                  </div>
                </div>
                
                <div v-if="!topQueues || topQueues.length === 0" class="text-center py-12">
                  <svg class="w-12 h-12 mx-auto text-gray-300 dark:text-gray-700 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                  </svg>
                  <p class="text-sm font-medium text-gray-500 dark:text-gray-400">No data available</p>
                </div>
              </div>
            </div>
          </div>
          
          <!-- Latency Percentiles Chart -->
          <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
            <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6">
              Processing Latency Percentiles
            </h2>
            <div class="h-64">
              <Bar v-if="latencyChartData" :data="latencyChartData" :options="chartOptions" />
            </div>
          </div>
          
          <!-- System Performance Metrics -->
          <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
            <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6">
              System Performance Metrics
            </h2>
            <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6">
              <div class="p-5 bg-gradient-to-br from-blue-50 to-cyan-50/50 dark:from-blue-900/20 dark:to-cyan-900/10 rounded-xl border border-blue-100 dark:border-blue-800/30">
                <div class="text-xs font-bold text-blue-600 dark:text-blue-400 uppercase tracking-wider mb-2">Messages Ingested</div>
                <div class="text-3xl font-black text-blue-900 dark:text-blue-100 metric-value">
                  {{ formatNumber(data?.messagesIngested || 0) }}
                </div>
              </div>
              
              <div class="p-5 bg-gradient-to-br from-emerald-50 to-teal-50/50 dark:from-emerald-900/20 dark:to-teal-900/10 rounded-xl border border-emerald-100 dark:border-emerald-800/30">
                <div class="text-xs font-bold text-emerald-600 dark:text-emerald-400 uppercase tracking-wider mb-2">Messages Processed</div>
                <div class="text-3xl font-black text-emerald-900 dark:text-emerald-100 metric-value">
                  {{ formatNumber(data?.messagesProcessed || 0) }}
                </div>
              </div>
              
              <div class="p-5 bg-gradient-to-br from-red-50 to-rose-50/50 dark:from-red-900/20 dark:to-rose-900/10 rounded-xl border border-red-100 dark:border-red-800/30">
                <div class="text-xs font-bold text-red-600 dark:text-red-400 uppercase tracking-wider mb-2">Messages Failed</div>
                <div class="text-3xl font-black text-red-900 dark:text-red-100 metric-value">
                  {{ formatNumber(data?.messagesFailed || 0) }}
                </div>
              </div>
              
              <div class="p-5 bg-gradient-to-br from-purple-50 to-pink-50/50 dark:from-purple-900/20 dark:to-pink-900/10 rounded-xl border border-purple-100 dark:border-purple-800/30">
                <div class="text-xs font-bold text-purple-600 dark:text-purple-400 uppercase tracking-wider mb-2">Success Rate</div>
                <div class="text-3xl font-black text-purple-900 dark:text-purple-100 metric-value">
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
import { ref, computed, onMounted, h } from 'vue';
import { Bar } from 'vue-chartjs';
import { Chart as ChartJS, CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend } from 'chart.js';
import AppLayout from '../components/layout/AppLayout.vue';
import MetricCardV2 from '../components/common/MetricCardV2.vue';
import LoadingState from '../components/common/LoadingState.vue';
import QuickActions from '../components/common/QuickActions.vue';
import { useApi } from '../composables/useApi';

ChartJS.register(CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend);

const RefreshIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15' })])};

const { loading, error, execute, client } = useApi();
const data = ref(null);
const allQueues = ref([]);
const selectedTimeRange = ref('24h');
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

const quickActions = computed(() => [
  {
    id: 'refresh',
    label: 'Refresh',
    icon: RefreshIcon,
    variant: 'secondary',
    onClick: fetchAnalytics,
    shortcut: 'R'
  }
]);

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
  const throughputSeries = apiData.throughput?.timeSeries || [];
  const interval = apiData.timeRange?.interval || 'hour';
  
  const labels = throughputSeries.map(item => {
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
  
  const latencyOverall = apiData.latency?.overall || {};
  
  const errorSeries = apiData.errorRates?.timeSeries || [];
  const failedCounts = errorSeries.map(item => item.failed || 0).reverse();
  
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
      p75: 0,
      p90: 0,
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

const topQueues = computed(() => {
  if (!data.value?.topQueues) return [];
  return data.value.topQueues.slice(0, 5);
});

const generateSparkline = (type) => {
  // Generate simple sparkline data for visual effect
  return Array(10).fill(0).map(() => Math.random() * 100);
};

const formatNumber = (num) => {
  if (num >= 1000000) return (num / 1000000).toFixed(1) + 'M';
  if (num >= 10000) return (num / 1000).toFixed(1) + 'K';
  return num.toLocaleString();
};

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
        borderRadius: 8
      },
      {
        label: 'Processed',
        data: processed,
        backgroundColor: 'rgba(16, 185, 129, 0.9)',
        borderColor: 'rgb(16, 185, 129)',
        borderWidth: 0,
        borderRadius: 8
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
        borderRadius: 8
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
        borderRadius: 8
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
        color: 'rgba(100, 116, 139, 0.9)',
        padding: 20,
        font: {
          size: 13,
          weight: '600',
          family: 'Inter, system-ui, sans-serif'
        },
        usePointStyle: true,
        pointStyle: 'circle',
        boxWidth: 8,
        boxHeight: 8
      }
    },
    tooltip: {
      backgroundColor: 'rgba(17, 24, 39, 0.95)',
      padding: 16,
      titleColor: 'rgba(255, 255, 255, 0.95)',
      titleFont: {
        size: 14,
        weight: '700'
      },
      bodyColor: 'rgba(255, 255, 255, 0.85)',
      bodyFont: {
        size: 13,
        weight: '500'
      },
      borderColor: 'rgba(99, 102, 241, 0.3)',
      borderWidth: 1,
      cornerRadius: 12,
      displayColors: true,
      boxWidth: 10,
      boxHeight: 10,
      boxPadding: 6
    }
  },
  scales: {
    y: {
      beginAtZero: true,
      grid: {
        color: 'rgba(148, 163, 184, 0.06)',
        lineWidth: 1
      },
      ticks: {
        color: 'rgba(100, 116, 139, 0.7)',
        font: {
          size: 12,
          weight: '500'
        },
        padding: 10
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
</script>

