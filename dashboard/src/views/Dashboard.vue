<template>
  <AppLayout>
    <LoadingState :loading="loading" :error="error" @retry="fetchData">
      <div v-if="data" class="space-y-6">
        <!-- Metrics Grid -->
        <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6">
          <MetricCard
            title="Total Messages"
            :value="data.totalMessages || 0"
            :icon="MessagesIcon"
            icon-color="blue"
          />
          
          <MetricCard
            title="Pending"
            :value="data.pendingMessages || 0"
            :icon="ClockIcon"
            icon-color="yellow"
          />
          
          <MetricCard
            title="Processing"
            :value="data.processingMessages || 0"
            :icon="CogIcon"
            icon-color="green"
          />
          
          <MetricCard
            title="Failed"
            :value="data.failedMessages || 0"
            :icon="ExclamationIcon"
            icon-color="red"
          />
        </div>
        
        <!-- Throughput Chart -->
        <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-emerald-500/5 transition-all duration-300">
          <div class="flex items-center justify-between mb-6">
            <h2 class="text-lg font-bold text-gray-900 dark:text-white">
              Message Distribution
            </h2>
            <div class="flex items-center space-x-2 text-sm text-gray-500 dark:text-gray-400">
              <div class="w-2 h-2 bg-emerald-500 rounded-full animate-pulse"></div>
              <span>Live</span>
            </div>
          </div>
          <div class="h-72">
            <Bar v-if="chartData" :data="chartData" :options="chartOptions" />
          </div>
        </div>
        
        <!-- Two Column Layout -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <!-- Active Queues -->
          <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-emerald-500/5 transition-all duration-300">
            <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-4">
              Active Queues
            </h2>
            <div class="space-y-2">
              <div 
                v-for="queue in activeQueues" 
                :key="queue.name"
                class="group flex items-center justify-between p-4 rounded-xl hover:bg-gradient-to-r hover:from-emerald-50 hover:to-transparent dark:hover:from-emerald-900/10 dark:hover:to-transparent cursor-pointer transition-all border border-transparent hover:border-emerald-200 dark:hover:border-emerald-800"
                @click="$router.push(`/queues/${queue.name}`)"
              >
                <div class="flex-1">
                  <div class="font-semibold text-gray-900 dark:text-white group-hover:text-emerald-600 dark:group-hover:text-emerald-400 transition-colors">
                    {{ queue.name }}
                  </div>
                  <div class="text-sm text-gray-500 dark:text-gray-400 mt-1">
                    {{ queue.partitions || 0 }} partitions
                  </div>
                </div>
                <div class="text-right">
                  <div class="text-sm font-semibold text-gray-900 dark:text-white mb-1">
                    {{ queue.pendingMessages || 0 }} pending
                  </div>
                  <StatusBadge :status="queue.status || 'active'" />
                </div>
              </div>
              
              <div v-if="!activeQueues || activeQueues.length === 0" class="text-center py-12">
                <svg class="w-12 h-12 mx-auto text-gray-300 dark:text-gray-700" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                </svg>
                <p class="mt-3 text-gray-500 dark:text-gray-400">No active queues</p>
              </div>
            </div>
          </div>
          
          <!-- System Stats -->
          <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-emerald-500/5 transition-all duration-300">
            <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-6">
              System Statistics
            </h2>
            <div class="space-y-4">
              <div class="flex justify-between items-center p-3 rounded-xl hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors">
                <span class="text-gray-600 dark:text-gray-400 font-medium">Total Queues</span>
                <span class="text-lg font-bold text-gray-900 dark:text-white">
                  {{ data.totalQueues || 0 }}
                </span>
              </div>
              <div class="flex justify-between items-center p-3 rounded-xl hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors">
                <span class="text-gray-600 dark:text-gray-400 font-medium">Active Leases</span>
                <span class="text-lg font-bold text-emerald-600 dark:text-emerald-400">
                  {{ data.activeLeases || 0 }}
                </span>
              </div>
              <div class="flex justify-between items-center p-3 rounded-xl hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors">
                <span class="text-gray-600 dark:text-gray-400 font-medium">Dead Letter Messages</span>
                <span class="text-lg font-bold text-red-600 dark:text-red-400">
                  {{ data.deadLetterMessages || 0 }}
                </span>
              </div>
              <div class="flex justify-between items-center p-3 rounded-xl hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors">
                <span class="text-gray-600 dark:text-gray-400 font-medium">Avg Processing Time</span>
                <span class="text-lg font-bold text-gray-900 dark:text-white">
                  {{ formatDuration(data.avgProcessingTime) }}
                </span>
              </div>
              <div class="flex justify-between items-center p-3 rounded-xl hover:bg-gray-50 dark:hover:bg-gray-800 transition-colors">
                <span class="text-gray-600 dark:text-gray-400 font-medium">System Uptime</span>
                <span class="text-lg font-bold text-gray-900 dark:text-white">
                  {{ formatDuration(data.uptime) }}
                </span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </LoadingState>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { Bar } from 'vue-chartjs';
import { Chart as ChartJS, CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend } from 'chart.js';
import AppLayout from '../components/layout/AppLayout.vue';
import MetricCard from '../components/common/MetricCard.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingState from '../components/common/LoadingState.vue';
import { useApi } from '../composables/useApi';
import { usePolling } from '../composables/usePolling';

// Register Chart.js components
ChartJS.register(CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend);

// Icons as render functions
import { h } from 'vue';

const MessagesIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M8 10h.01M12 10h.01M16 10h.01M9 16H5a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v8a2 2 0 01-2 2h-5l-5 5v-5z' })
  ])
};

const ClockIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z' })
  ])
};

const CogIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z' }),
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M15 12a3 3 0 11-6 0 3 3 0 016 0z' })
  ])
};

const ExclamationIcon = {
  render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [
    h('path', { 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'stroke-width': '2', d: 'M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z' })
  ])
};

const { loading, error, execute, client } = useApi();
const data = ref(null);

const fetchData = async () => {
  try {
    const result = await execute(client.getStatus.bind(client));
    data.value = result || {
      totalMessages: 0,
      pendingMessages: 0,
      processingMessages: 0,
      failedMessages: 0,
      completedMessages: 0,
      totalQueues: 0,
      activeLeases: 0,
      deadLetterMessages: 0,
      avgProcessingTime: 0,
      uptime: 0,
      queues: []
    };
  } catch (err) {
    console.error('Failed to fetch dashboard data:', err);
    // Set empty data on error so charts still render
    data.value = {
      totalMessages: 0,
      pendingMessages: 0,
      processingMessages: 0,
      failedMessages: 0,
      completedMessages: 0,
      totalQueues: 0,
      activeLeases: 0,
      deadLetterMessages: 0,
      avgProcessingTime: 0,
      uptime: 0,
      queues: []
    };
  }
};

// Auto-refresh is optional
const { startPolling, stopPolling } = usePolling(fetchData, 5000);

const activeQueues = computed(() => {
  if (!data.value?.queues) return [];
  return data.value.queues.filter(q => (q.pendingMessages || 0) > 0).slice(0, 5);
});

const chartData = computed(() => {
  return {
    labels: ['Pending', 'Processing', 'Completed', 'Failed'],
    datasets: [
      {
        label: 'Messages',
        data: [
          data.value?.pendingMessages || 0,
          data.value?.processingMessages || 0,
          data.value?.completedMessages || 0,
          data.value?.failedMessages || 0
        ],
        backgroundColor: [
          'rgba(245, 158, 11, 0.9)',    // Amber
          'rgba(59, 130, 246, 0.9)',    // Blue
          'rgba(16, 185, 129, 0.9)',    // Emerald
          'rgba(239, 68, 68, 0.9)'      // Red
        ],
        borderColor: [
          'rgb(245, 158, 11)',
          'rgb(59, 130, 246)',
          'rgb(16, 185, 129)',
          'rgb(239, 68, 68)'
        ],
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
      display: false
    },
    title: {
      display: false
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
          size: 12,
          weight: '500'
        }
      },
      border: {
        display: false
      }
    }
  }
};

const formatDuration = (ms) => {
  if (!ms) return 'N/A';
  
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  const days = Math.floor(hours / 24);
  
  if (days > 0) return `${days}d ${hours % 24}h`;
  if (hours > 0) return `${hours}h ${minutes % 60}m`;
  if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
  return `${seconds}s`;
};

onMounted(async () => {
  await fetchData();
  // Don't start polling by default - user can enable in settings
});

onUnmounted(() => {
  stopPolling();
});
</script>

