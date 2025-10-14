<template>
  <AppLayout>
    <LoadingState :loading="loading" :error="error" @retry="fetchQueueDetail">
      <div v-if="data" class="space-y-6">
        <!-- Header -->
        <div class="flex items-center justify-between">
          <div>
            <button 
              @click="$router.back()"
              class="text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white mb-2 inline-flex items-center"
            >
              <svg class="w-5 h-5 mr-1" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
              </svg>
              Back to Queues
            </button>
            <h1 class="text-3xl font-bold text-gray-900 dark:text-white">
              {{ queueName }}
            </h1>
            <p v-if="data.namespace" class="text-gray-600 dark:text-gray-400 mt-1">
              Namespace: {{ data.namespace }}
            </p>
          </div>
          
          <button
            @click="$router.push(`/queues/${queueName}/messages`)"
            class="px-4 py-2 bg-primary-600 hover:bg-primary-700 text-white rounded-lg transition-colors"
          >
            View Messages
          </button>
        </div>
        
        <!-- Summary Cards -->
        <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-5 gap-6">
          <MetricCard
            title="Total Messages"
            :value="data.totalMessages || 0"
          />
          
          <MetricCard
            title="Pending"
            :value="data.pendingMessages || 0"
            icon-color="yellow"
          />
          
          <MetricCard
            title="Processing"
            :value="data.processingMessages || 0"
            icon-color="blue"
          />
          
          <MetricCard
            title="Completed"
            :value="data.completedMessages || 0"
            icon-color="green"
          />
          
          <MetricCard
            title="Failed"
            :value="data.failedMessages || 0"
            icon-color="red"
          />
        </div>
        
        <!-- Configuration -->
        <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6">
          <h2 class="text-lg font-semibold text-gray-900 dark:text-white mb-4">
            Configuration
          </h2>
          <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div>
              <span class="text-gray-600 dark:text-gray-400 text-sm">Lease Time</span>
              <p class="text-lg font-medium text-gray-900 dark:text-white mt-1">
                {{ data.leaseTime ? `${data.leaseTime}s` : 'N/A' }}
              </p>
            </div>
            <div>
              <span class="text-gray-600 dark:text-gray-400 text-sm">Retry Limit</span>
              <p class="text-lg font-medium text-gray-900 dark:text-white mt-1">
                {{ data.retryLimit || 'N/A' }}
              </p>
            </div>
            <div>
              <span class="text-gray-600 dark:text-gray-400 text-sm">TTL</span>
              <p class="text-lg font-medium text-gray-900 dark:text-white mt-1">
                {{ formatTTL(data.ttl) }}
              </p>
            </div>
          </div>
        </div>
        
        <!-- Partitions -->
        <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6">
          <h2 class="text-lg font-semibold text-gray-900 dark:text-white mb-4">
            Partitions
          </h2>
          
          <div class="overflow-x-auto">
            <table class="w-full">
              <thead class="bg-gray-50 dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700">
                <tr>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Partition
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Pending
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Processing
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Completed
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Failed
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Lag
                  </th>
                </tr>
              </thead>
              <tbody class="divide-y divide-gray-200 dark:divide-gray-700">
                <tr 
                  v-for="partition in data.partitions" 
                  :key="partition.partition"
                  class="hover:bg-gray-50 dark:hover:bg-gray-800"
                >
                  <td class="px-6 py-4 whitespace-nowrap text-sm font-medium text-gray-900 dark:text-white">
                    Partition {{ partition.partition }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ (partition.pending || 0).toLocaleString() }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ (partition.processing || 0).toLocaleString() }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ (partition.completed || 0).toLocaleString() }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ (partition.failed || 0).toLocaleString() }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm">
                    <span :class="getLagColor(partition.lag)">
                      {{ partition.lagFormatted || 'N/A' }}
                    </span>
                  </td>
                </tr>
              </tbody>
            </table>
            
            <div v-if="!data.partitions || data.partitions.length === 0" class="text-center py-12">
              <p class="text-gray-500 dark:text-gray-400">No partition data available</p>
            </div>
          </div>
        </div>
        
        <!-- Status Distribution Chart -->
        <div class="bg-white dark:bg-gray-900 rounded-2xl border border-gray-200 dark:border-gray-800 p-6 shadow-sm hover:shadow-xl hover:shadow-primary-500/5 transition-all duration-300">
          <h2 class="text-lg font-bold text-gray-900 dark:text-white mb-6">
            Message Distribution
          </h2>
          <div class="h-64">
            <Bar v-if="chartData" :data="chartData" :options="chartOptions" />
          </div>
        </div>
      </div>
    </LoadingState>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { useRoute } from 'vue-router';
import { Bar } from 'vue-chartjs';
import { Chart as ChartJS, CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend } from 'chart.js';
import AppLayout from '../components/layout/AppLayout.vue';
import MetricCard from '../components/common/MetricCard.vue';
import LoadingState from '../components/common/LoadingState.vue';
import { useApi } from '../composables/useApi';
import { usePolling } from '../composables/usePolling';

ChartJS.register(CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend);

const route = useRoute();
const queueName = computed(() => route.params.queueName);

const { loading, error, execute, client } = useApi();
const data = ref(null);

const fetchQueueDetail = async () => {
  try {
    const result = await execute(client.getQueueDetail.bind(client), queueName.value);
    if (result) {
      // Map API response structure properly
      data.value = {
        name: result.queue?.name || queueName.value,
        namespace: result.queue?.namespace,
        task: result.queue?.task,
        totalMessages: result.totals?.messages?.total || 0,
        pendingMessages: result.totals?.messages?.pending || 0,
        processingMessages: result.totals?.messages?.processing || 0,
        completedMessages: result.totals?.messages?.completed || 0,
        failedMessages: result.totals?.messages?.failed || 0,
        leaseTime: result.queue?.config?.leaseTime,
        retryLimit: result.queue?.config?.retryLimit,
        ttl: result.queue?.config?.ttl,
        partitions: (result.partitions || []).map(p => ({
          partition: p.name || p.id,
          pending: p.messages?.pending || 0,
          processing: p.messages?.processing || 0,
          completed: p.messages?.completed || 0,
          failed: p.messages?.failed || 0,
          lag: p.lag?.seconds || 0,
          lagFormatted: p.lag?.formatted || 'N/A'
        }))
      };
    } else {
      data.value = getEmptyData();
    }
  } catch (err) {
    console.error('Failed to fetch queue detail:', err);
    data.value = getEmptyData();
  }
};

const getEmptyData = () => ({
  name: queueName.value,
  namespace: null,
  task: null,
  totalMessages: 0,
  pendingMessages: 0,
  processingMessages: 0,
  completedMessages: 0,
  failedMessages: 0,
  leaseTime: null,
  retryLimit: null,
  ttl: null,
  partitions: []
});

const { startPolling, stopPolling } = usePolling(fetchQueueDetail, 5000);

// Format TTL in human-readable format
const formatTTL = (seconds) => {
  if (!seconds) return 'N/A';
  if (seconds < 60) return `${seconds}s`;
  if (seconds < 3600) {
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return secs > 0 ? `${mins}m ${secs}s` : `${mins}m`;
  }
  const hours = Math.floor(seconds / 3600);
  const mins = Math.floor((seconds % 3600) / 60);
  return mins > 0 ? `${hours}h ${mins}m` : `${hours}h`;
};

const getLagColor = (lag) => {
  if (!lag || lag === 0) return 'text-gray-900 dark:text-white';
  if (lag < 100) return 'text-primary-600 dark:text-primary-400 font-semibold';
  if (lag < 1000) return 'text-yellow-600 dark:text-yellow-400 font-semibold';
  return 'text-red-600 dark:text-red-400 font-semibold';
};

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
          'rgba(245, 158, 11, 0.9)',
          'rgba(59, 130, 246, 0.9)',
          'rgba(16, 185, 129, 0.9)',
          'rgba(239, 68, 68, 0.9)'
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

onMounted(async () => {
  await fetchQueueDetail();
  // Don't auto-refresh by default
});

onUnmounted(() => {
  stopPolling();
});
</script>

