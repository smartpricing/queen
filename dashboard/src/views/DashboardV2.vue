<template>
  <AppLayout>
    <!-- Command Palette -->
    <CommandPalette 
      v-model="showCommandPalette" 
      @command="handleCommand"
    />
    
    <LoadingState :loading="loading" :error="error" @retry="fetchData">
      <div v-if="data" class="space-y-6">
        <!-- Header with Quick Actions -->
        <div class="flex items-center justify-between">
          <div>
            <h1 class="text-3xl font-black text-gray-900 dark:text-white mb-1">
              Dashboard
            </h1>
            <p class="text-sm text-gray-600 dark:text-gray-400">
              System overview
            </p>
          </div>
          
          <div class="flex items-center gap-3">
            <!-- Time Range Selector (like Kafka dashboard) -->
            <div class="flex items-center gap-2">
              <button
                v-for="range in timeRanges"
                :key="range"
                @click="selectedTimeRange = range"
                :class="[
                  'px-3 py-1.5 text-xs font-bold rounded-lg',
                  selectedTimeRange === range
                    ? 'bg-primary-600 text-white'
                    : 'bg-gray-200 dark:bg-gray-800 text-gray-700 dark:text-gray-400 hover:bg-gray-300 dark:hover:bg-gray-700'
                ]"
              >
                {{ range }}
              </button>
            </div>
            
            <QuickActions :actions="quickActions" />
          </div>
        </div>
        
        <!-- Enhanced Metrics Grid with Sparklines -->
        <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-5 gap-4">
          <MetricCardV2
            title="Total Messages"
            :value="data.totalMessages || 0"
            :icon="MessagesIcon"
            icon-color="blue"
            :sparkline-data="generateSparkline(data.totalMessages)"
            :trend="calculateTrend(data.totalMessages, previousData?.totalMessages)"
          />
          
          <MetricCardV2
            title="Pending"
            :value="data.pendingMessages || 0"
            :icon="ClockIcon"
            icon-color="yellow"
            :sparkline-data="generateSparkline(data.pendingMessages)"
            :trend="calculateTrend(data.pendingMessages, previousData?.pendingMessages)"
          />
          
          <MetricCardV2
            title="Completed"
            :value="data.completedMessages || 0"
            :icon="CheckIcon"
            icon-color="green"
            :sparkline-data="generateSparkline(data.completedMessages)"
            :trend="calculateTrend(data.completedMessages, previousData?.completedMessages)"
          />
          
          <MetricCardV2
            title="Failed"
            :value="data.failedMessages || 0"
            :icon="ExclamationIcon"
            icon-color="red"
            :sparkline-data="generateSparkline(data.failedMessages)"
            :trend="calculateTrend(data.failedMessages, previousData?.failedMessages)"
          />
          
          <MetricCardV2
            title="Dead Letter Queue"
            :value="data.deadLetterMessages || 0"
            :icon="DLQIcon"
            icon-color="purple"
            :sparkline-data="generateSparkline(data.deadLetterMessages)"
            :trend="calculateTrend(data.deadLetterMessages, previousData?.deadLetterMessages)"
          />
        </div>
        
        <!-- Charts Side-by-Side (like Kafka dashboard) -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <!-- Message States Chart -->
          <div class="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 p-6 shadow-lg">
            <div class="mb-6">
              <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-1">
                Message States
              </h2>
              <p class="text-sm text-gray-600 dark:text-gray-400">
                Distribution over time
              </p>
            </div>
            <div class="h-64">
              <Line v-if="chartData" :data="chartData" :options="lineChartOptions" />
            </div>
          </div>
          
          <!-- System Health Chart -->
          <div class="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 p-6 shadow-lg">
            <div class="mb-6">
              <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-1">
                System Activity
              </h2>
              <p class="text-sm text-gray-600 dark:text-gray-400">
                Queues and processing
              </p>
            </div>
            <div class="h-64">
              <Line v-if="systemChartData" :data="systemChartData" :options="lineChartOptions" />
            </div>
          </div>
        </div>
        
        <!-- Two Column Layout -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <!-- Active Queues with Enhanced Styling -->
          <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
            <div class="flex items-center justify-between mb-5">
              <div>
                <h2 class="text-xl font-black text-gray-900 dark:text-white mb-1">
                  Active Queues
                </h2>
                <p class="text-sm text-gray-600 dark:text-gray-400">
                  {{ activeQueues.length }} queue{{ activeQueues.length !== 1 ? 's' : '' }} active
                </p>
              </div>
              <button
                @click="$router.push('/queues')"
                class="text-sm font-semibold text-primary-600 hover:text-primary-700 dark:text-primary-400 dark:hover:text-primary-300 transition-colors"
              >
                View all →
              </button>
            </div>
            
            <div class="space-y-2">
              <div 
                v-for="queue in activeQueues" 
                :key="queue.name"
                class="group flex items-center justify-between p-4 rounded-xl hover:bg-primary-50/50 dark:hover:bg-primary-900/10 cursor-pointer border border-transparent hover:border-primary-200/50 dark:hover:border-primary-800/50"
                @click="$router.push(`/queues/${queue.name}`)"
              >
                <div class="flex items-center gap-4 flex-1 min-w-0">
                  <div class="flex-shrink-0 w-10 h-10 rounded-xl bg-gradient-to-br from-primary-500 to-blue-600 flex items-center justify-center text-white font-bold shadow-lg shadow-primary-500/30">
                    {{ queue.name.substring(0, 2).toUpperCase() }}
                  </div>
                  <div class="flex-1 min-w-0">
                    <div class="text-sm font-bold text-gray-900 dark:text-white group-hover:text-primary-600 dark:group-hover:text-primary-400 transition-colors truncate">
                      {{ queue.name }}
                    </div>
                    <div class="text-xs text-gray-500 dark:text-gray-400 mt-0.5">
                      {{ queue.partitions || 0 }} partition{{ queue.partitions !== 1 ? 's' : '' }}
                    </div>
                  </div>
                </div>
                <div class="flex items-center gap-3">
                  <div class="text-right">
                    <div class="text-sm font-black text-gray-900 dark:text-white">
                      {{ formatNumber(queue.pendingMessages || 0) }}
                    </div>
                    <div class="text-xs text-gray-500 dark:text-gray-400">
                      pending
                    </div>
                  </div>
                  <StatusBadge :status="queue.status || 'active'" />
                </div>
              </div>
              
              <div v-if="!activeQueues || activeQueues.length === 0" class="text-center py-12">
                <svg class="w-12 h-12 mx-auto text-gray-300 dark:text-gray-700 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                </svg>
                <p class="text-sm font-medium text-gray-500 dark:text-gray-400">No active queues</p>
                <p class="text-xs text-gray-400 dark:text-gray-500 mt-1">Create your first queue to get started</p>
              </div>
            </div>
          </div>
          
          <!-- System Stats with Modern Cards -->
          <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
            <div class="mb-5">
              <h2 class="text-xl font-black text-gray-900 dark:text-white mb-1">
                System Statistics
              </h2>
              <p class="text-sm text-gray-600 dark:text-gray-400">
                Current system performance metrics
              </p>
            </div>
            
            <div class="grid grid-cols-2 gap-3">
              <div class="p-4 bg-gradient-to-br from-blue-50 to-cyan-50/50 dark:from-blue-900/20 dark:to-cyan-900/10 rounded-xl border border-blue-100 dark:border-blue-800/30">
                <div class="text-xs font-bold text-blue-600 dark:text-blue-400 uppercase tracking-wider mb-2">Total Queues</div>
                <div class="text-3xl font-black text-blue-900 dark:text-blue-100 metric-value">
                  {{ data.totalQueues || 0 }}
                </div>
              </div>
              
              <div class="p-4 bg-gradient-to-br from-purple-50 to-pink-50/50 dark:from-purple-900/20 dark:to-pink-900/10 rounded-xl border border-purple-100 dark:border-purple-800/30">
                <div class="text-xs font-bold text-purple-600 dark:text-purple-400 uppercase tracking-wider mb-2">Active Leases</div>
                <div class="text-3xl font-black text-purple-900 dark:text-purple-100 metric-value">
                  {{ data.activeLeases || 0 }}
                </div>
              </div>
              
              <div class="p-4 bg-gradient-to-br from-emerald-50 to-teal-50/50 dark:from-emerald-900/20 dark:to-teal-900/10 rounded-xl border border-emerald-100 dark:border-emerald-800/30">
                <div class="text-xs font-bold text-emerald-600 dark:text-emerald-400 uppercase tracking-wider mb-2">Processing</div>
                <div class="text-3xl font-black text-emerald-900 dark:text-emerald-100 metric-value">
                  {{ data.processingMessages || 0 }}
                </div>
              </div>
              
              <div class="p-4 bg-gradient-to-br from-amber-50 to-orange-50/50 dark:from-amber-900/20 dark:to-orange-900/10 rounded-xl border border-amber-100 dark:border-amber-800/30">
                <div class="text-xs font-bold text-amber-600 dark:text-amber-400 uppercase tracking-wider mb-2">Messages/Sec</div>
                <div class="text-3xl font-black text-amber-900 dark:text-amber-100 metric-value">
                  {{ throughputPerSecond }}
                </div>
              </div>
            </div>
            
            <!-- Mini Performance Indicators -->
            <div class="mt-4 pt-4 border-t border-gray-200 dark:border-gray-800 space-y-3">
              <div class="flex items-center justify-between">
                <span class="text-xs font-semibold text-gray-600 dark:text-gray-400">System Health</span>
                <div class="flex items-center gap-2">
                  <div class="h-2 flex-1 w-32 bg-gray-200 dark:bg-gray-800 rounded-full overflow-hidden">
                    <div class="h-full bg-gradient-to-r from-emerald-500 to-teal-500 rounded-full" style="width: 95%"></div>
                  </div>
                  <span class="text-xs font-bold text-emerald-600 dark:text-emerald-400">95%</span>
                </div>
              </div>
              
              <div class="flex items-center justify-between">
                <span class="text-xs font-semibold text-gray-600 dark:text-gray-400">Queue Utilization</span>
                <div class="flex items-center gap-2">
                  <div class="h-2 flex-1 w-32 bg-gray-200 dark:bg-gray-800 rounded-full overflow-hidden">
                    <div class="h-full bg-gradient-to-r from-blue-500 to-cyan-500 rounded-full" :style="`width: ${queueUtilization}%`"></div>
                  </div>
                  <span class="text-xs font-bold text-blue-600 dark:text-blue-400">{{ queueUtilization }}%</span>
                </div>
              </div>
            </div>
          </div>
        </div>
        
        <!-- Keyboard Shortcut Hint -->
        <div class="flex items-center justify-center">
          <button
            @click="showCommandPalette = true"
            class="group flex items-center gap-3 px-4 py-2 bg-gray-100/80 dark:bg-gray-800/80 hover:bg-gray-200 dark:hover:bg-gray-700 rounded-xl transition-all duration-200 border border-gray-200 dark:border-gray-700"
          >
            <svg class="w-4 h-4 text-gray-600 dark:text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
            </svg>
            <span class="text-sm font-medium text-gray-700 dark:text-gray-300">Quick actions</span>
            <kbd class="px-2 py-1 text-xs font-mono font-semibold text-gray-600 dark:text-gray-400 bg-white dark:bg-gray-900 border border-gray-300 dark:border-gray-600 rounded">⌘K</kbd>
          </button>
        </div>
      </div>
    </LoadingState>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, h } from 'vue';
import { Line } from 'vue-chartjs';
import { Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend, Filler } from 'chart.js';
import AppLayout from '../components/layout/AppLayout.vue';
import MetricCardV2 from '../components/common/MetricCardV2.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingState from '../components/common/LoadingState.vue';
import CommandPalette from '../components/common/CommandPalette.vue';
import QuickActions from '../components/common/QuickActions.vue';
import { useApi } from '../composables/useApi';
import { usePolling } from '../composables/usePolling';
import { useColorMode } from '../composables/useColorMode';

// Register Chart.js components
ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend, Filler);

// Icons
const MessagesIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M8 10h.01M12 10h.01M16 10h.01M9 16H5a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v8a2 2 0 01-2 2h-5l-5 5v-5z' })])};
const ClockIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z' })])};
const CheckIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z' })])};
const ExclamationIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z' })])};
const DLQIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4' })])};
const RefreshIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15' })])};

const { loading, error, execute, client } = useApi();
const { colorMode, toggleColorMode } = useColorMode();
const data = ref(null);
const previousData = ref(null);
const showCommandPalette = ref(false);
const history = ref([]);
const timeRanges = ['1H', '1D', '7D', '30D'];
const selectedTimeRange = ref('1D');

// Quick actions
const quickActions = computed(() => [
  {
    id: 'refresh',
    label: 'Refresh',
    icon: RefreshIcon,
    variant: 'secondary',
    onClick: fetchData,
    shortcut: 'R'
  }
]);

const fetchData = async () => {
  try {
    previousData.value = data.value ? { ...data.value } : null;
    
    const result = await execute(client.getStatus.bind(client));
    if (result) {
      const total = result.messages?.total || 0;
      const pending = result.messages?.pending || 0;
      const processing = result.messages?.processing || 0;
      const completed = result.messages?.completed || 0;
      const failed = result.messages?.failed || 0;
      
      const newData = {
        totalMessages: total,
        pendingMessages: pending,
        processingMessages: processing,
        failedMessages: failed,
        completedMessages: completed,
        totalQueues: result.queues?.length || 0,
        activeLeases: result.leases?.active || 0,
        deadLetterMessages: result.deadLetterQueue?.totalMessages || 0,
        queues: result.queues || [],
        timestamp: Date.now()
      };
      
      data.value = newData;
      
      // Store in history for sparklines (keep last 20 data points)
      history.value.push(newData);
      if (history.value.length > 20) {
        history.value.shift();
      }
    }
  } catch (err) {
    console.error('Failed to fetch dashboard data:', err);
  }
};

// Polling disabled per user request
// const { startPolling, stopPolling } = usePolling(fetchData, 3000);

const activeQueues = computed(() => {
  if (!data.value?.queues || data.value.queues.length === 0) return [];
  
  // Show all queues, sorted by pending messages (most active first)
  return data.value.queues
    .map(q => ({
      name: q.name,
      namespace: q.namespace,
      partitions: q.partitions || 0,
      pendingMessages: q.pending || 0,
      status: q.pending > 0 ? 'active' : 'inactive'
    }))
    .sort((a, b) => (b.pendingMessages || 0) - (a.pendingMessages || 0))
    .slice(0, 10); // Show top 10 queues
});

// Generate sparkline data from history
const generateSparkline = (currentValue) => {
  if (history.value.length < 2) {
    // Generate some mock variation for initial display
    return Array(10).fill(0).map((_, i) => currentValue * (0.8 + Math.random() * 0.4));
  }
  
  // Return actual history (last 15 points)
  return history.value.slice(-15).map(h => {
    if (currentValue === data.value?.totalMessages) return h.totalMessages || 0;
    if (currentValue === data.value?.pendingMessages) return h.pendingMessages || 0;
    if (currentValue === data.value?.completedMessages) return h.completedMessages || 0;
    if (currentValue === data.value?.failedMessages) return h.failedMessages || 0;
    if (currentValue === data.value?.deadLetterMessages) return h.deadLetterMessages || 0;
    return 0;
  });
};

// Calculate trend percentage
const calculateTrend = (current, previous) => {
  if (!previous || previous === 0) return null;
  const change = ((current - previous) / previous) * 100;
  return Math.round(change);
};

// Format number
const formatNumber = (num) => {
  if (num >= 1000000) return (num / 1000000).toFixed(1) + 'M';
  if (num >= 10000) return (num / 1000).toFixed(1) + 'K';
  return num.toLocaleString();
};

// Throughput calculation
const throughputPerSecond = computed(() => {
  if (history.value.length < 2) return 0;
  
  const recent = history.value.slice(-2);
  const timeDiff = (recent[1].timestamp - recent[0].timestamp) / 1000;
  const messageDiff = (recent[1].completedMessages || 0) - (recent[0].completedMessages || 0);
  
  return Math.round(messageDiff / timeDiff) || 0;
});

// Queue utilization
const queueUtilization = computed(() => {
  if (!data.value?.totalMessages || data.value.totalMessages === 0) return 0;
  const active = (data.value.pendingMessages + data.value.processingMessages) || 0;
  return Math.min(Math.round((active / data.value.totalMessages) * 100), 100);
});

// Chart data for line chart
const chartData = computed(() => {
  const labels = history.value.length > 0
    ? history.value.map((_, i) => `${i * 3}s`)
    : ['0s'];
    
  const pending = history.value.map(h => h.pendingMessages || 0);
  const processing = history.value.map(h => h.processingMessages || 0);
  const completed = history.value.map(h => h.completedMessages || 0);
  const failed = history.value.map(h => h.failedMessages || 0);
  
  return {
    labels,
    datasets: [
      {
        label: 'Pending',
        data: pending.length > 0 ? pending : [data.value?.pendingMessages || 0],
        borderColor: 'rgb(245, 158, 11)',
        backgroundColor: 'rgba(245, 158, 11, 0.1)',
        fill: true,
        tension: 0.4,
        pointRadius: 0,
        pointHoverRadius: 6,
        borderWidth: 3
      },
      {
        label: 'Processing',
        data: processing.length > 0 ? processing : [data.value?.processingMessages || 0],
        borderColor: 'rgb(59, 130, 246)',
        backgroundColor: 'rgba(59, 130, 246, 0.1)',
        fill: true,
        tension: 0.4,
        pointRadius: 0,
        pointHoverRadius: 6,
        borderWidth: 3
      },
      {
        label: 'Completed',
        data: completed.length > 0 ? completed : [data.value?.completedMessages || 0],
        borderColor: 'rgb(16, 185, 129)',
        backgroundColor: 'rgba(16, 185, 129, 0.1)',
        fill: true,
        tension: 0.4,
        pointRadius: 0,
        pointHoverRadius: 6,
        borderWidth: 3
      },
      {
        label: 'Failed',
        data: failed.length > 0 ? failed : [data.value?.failedMessages || 0],
        borderColor: 'rgb(239, 68, 68)',
        backgroundColor: 'rgba(239, 68, 68, 0.1)',
        fill: true,
        tension: 0.4,
        pointRadius: 0,
        pointHoverRadius: 6,
        borderWidth: 3
      }
    ]
  };
});

const lineChartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: {
    mode: 'index',
    intersect: false,
  },
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
      enabled: true,
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
          size: 12,
          weight: '500'
        },
        maxRotation: 0
      },
      border: {
        display: false
      }
    }
  }
};

// System activity chart
const systemChartData = computed(() => {
  const labels = history.value.length > 0
    ? history.value.map((_, i) => `${i * 3}s`)
    : ['0s'];
    
  const activeLeases = history.value.map(h => h.activeLeases || 0);
  const totalQueues = history.value.map(h => h.totalQueues || 0);
  
  return {
    labels,
    datasets: [
      {
        label: 'Active Leases',
        data: activeLeases.length > 0 ? activeLeases : [data.value?.activeLeases || 0],
        borderColor: 'rgb(139, 92, 246)',
        backgroundColor: 'rgba(139, 92, 246, 0.1)',
        fill: true,
        tension: 0.4,
        pointRadius: 0,
        pointHoverRadius: 6,
        borderWidth: 3
      },
      {
        label: 'Total Queues',
        data: totalQueues.length > 0 ? totalQueues : [data.value?.totalQueues || 0],
        borderColor: 'rgb(59, 130, 246)',
        backgroundColor: 'rgba(59, 130, 246, 0.1)',
        fill: true,
        tension: 0.4,
        pointRadius: 0,
        pointHoverRadius: 6,
        borderWidth: 3
      }
    ]
  };
});

// Command palette handlers
const handleCommand = (command) => {
  switch (command) {
    case 'refresh':
      fetchData();
      break;
    case 'theme':
      toggleColorMode();
      break;
    case 'search':
      // Implement search functionality
      break;
  }
};

// Keyboard shortcuts
const handleKeyDown = (e) => {
  // Cmd/Ctrl + K for command palette
  if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
    e.preventDefault();
    showCommandPalette.value = !showCommandPalette.value;
  }
  
  // R for refresh (when not in input)
  if (e.key === 'r' && !['INPUT', 'TEXTAREA'].includes(e.target.tagName)) {
    e.preventDefault();
    fetchData();
  }
};

onMounted(async () => {
  await fetchData();
  // Auto-refresh disabled per user request
  // startPolling();
  window.addEventListener('keydown', handleKeyDown);
});

onUnmounted(() => {
  // stopPolling();
  window.removeEventListener('keydown', handleKeyDown);
});
</script>


