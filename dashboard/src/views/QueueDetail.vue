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
            class="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded-lg transition-colors"
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
                {{ data.leaseTime || 'N/A' }}ms
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
                {{ data.ttl || 'N/A' }}ms
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
                      {{ (partition.lag || 0).toLocaleString() }}
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
        <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-6">
          <h2 class="text-lg font-semibold text-gray-900 dark:text-white mb-4">
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
    data.value = result || {
      totalMessages: 0,
      pendingMessages: 0,
      processingMessages: 0,
      completedMessages: 0,
      failedMessages: 0,
      partitions: []
    };
  } catch (err) {
    console.error('Failed to fetch queue detail:', err);
    data.value = {
      totalMessages: 0,
      pendingMessages: 0,
      processingMessages: 0,
      completedMessages: 0,
      failedMessages: 0,
      partitions: []
    };
  }
};

const { startPolling, stopPolling } = usePolling(fetchQueueDetail, 5000);

const getLagColor = (lag) => {
  if (!lag || lag === 0) return 'text-gray-900 dark:text-white';
  if (lag < 100) return 'text-green-600 dark:text-green-400';
  if (lag < 1000) return 'text-yellow-600 dark:text-yellow-400';
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
          'rgba(234, 179, 8, 0.8)',
          'rgba(34, 197, 94, 0.8)',
          'rgba(59, 130, 246, 0.8)',
          'rgba(239, 68, 68, 0.8)'
        ],
        borderColor: [
          'rgb(234, 179, 8)',
          'rgb(34, 197, 94)',
          'rgb(59, 130, 246)',
          'rgb(239, 68, 68)'
        ],
        borderWidth: 2
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
    }
  },
  scales: {
    y: {
      beginAtZero: true,
      grid: {
        color: 'rgba(156, 163, 175, 0.1)'
      },
      ticks: {
        color: 'rgba(156, 163, 175, 0.8)'
      }
    },
    x: {
      grid: {
        display: false
      },
      ticks: {
        color: 'rgba(156, 163, 175, 0.8)'
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

