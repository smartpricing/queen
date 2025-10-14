<template>
  <AppLayout>
    <LoadingState :loading="loading" :error="error" @retry="fetchQueueDetail">
      <div v-if="data" class="space-y-6">
        <!-- Header with Back Button -->
        <div class="flex items-center justify-between">
          <div>
            <button 
              @click="$router.back()"
              class="text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white mb-2 inline-flex items-center text-sm font-medium"
            >
              <svg class="w-4 h-4 mr-1" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M15 19l-7-7 7-7" />
              </svg>
              Back to Queues
            </button>
            <h1 class="text-3xl font-black text-gray-900 dark:text-white mb-1">
              {{ queueName }}
            </h1>
            <p class="text-sm text-gray-600 dark:text-gray-400">
              {{ data.namespace ? `Namespace: ${data.namespace}` : 'Queue details and configuration' }}
            </p>
          </div>
          
          <QuickActions :actions="quickActions" />
        </div>
        
        <!-- Stats Bar (like Kafka dashboard) -->
        <StatsBar :stats="statsBarData" />
        
        <!-- Tab Navigation -->
        <TabNavigation
          v-model="activeTab"
          :tabs="tabs"
        />
        
        <!-- Tab Content -->
        <div v-if="activeTab === 'overview'">
          <!-- Metrics Grid -->
          <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-5 gap-4 mb-6">
            <MetricCardV2
              title="Total Messages"
              :value="data.totalMessages || 0"
              icon-color="blue"
            />
            
            <MetricCardV2
              title="Pending"
              :value="data.pendingMessages || 0"
              icon-color="yellow"
            />
            
            <MetricCardV2
              title="Processing"
              :value="data.processingMessages || 0"
              icon-color="blue"
            />
            
            <MetricCardV2
              title="Completed"
              :value="data.completedMessages || 0"
              icon-color="green"
            />
            
            <MetricCardV2
              title="Failed"
              :value="data.failedMessages || 0"
              icon-color="red"
            />
          </div>
          
          <!-- Charts Side-by-Side (like Kafka) -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
            <div class="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 p-6 shadow-lg">
              <h3 class="text-lg font-bold text-gray-900 dark:text-white mb-4">
                Message Rate
              </h3>
              <div class="h-64">
                <!-- Chart placeholder -->
                <div class="flex items-center justify-center h-full text-gray-400">
                  Chart coming soon
                </div>
              </div>
            </div>
            
            <div class="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 p-6 shadow-lg">
              <h3 class="text-lg font-bold text-gray-900 dark:text-white mb-4">
                Processing Time
              </h3>
              <div class="h-64">
                <!-- Chart placeholder -->
                <div class="flex items-center justify-center h-full text-gray-400">
                  Chart coming soon
                </div>
              </div>
            </div>
          </div>
        </div>
        
        <!-- Configuration Tab -->
        <div v-if="activeTab === 'configuration'">
          <div class="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 p-6 shadow-lg">
            <h2 class="text-xl font-black text-gray-900 dark:text-white mb-6">
              Queue Configuration
            </h2>
            <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
              <div>
                <label class="text-sm font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider">Lease Time</label>
                <p class="text-2xl font-black text-gray-900 dark:text-white mt-2">
                  {{ data.leaseTime ? `${data.leaseTime}s` : 'N/A' }}
                </p>
              </div>
              <div>
                <label class="text-sm font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider">Retry Limit</label>
                <p class="text-2xl font-black text-gray-900 dark:text-white mt-2">
                  {{ data.retryLimit || 'N/A' }}
                </p>
              </div>
              <div>
                <label class="text-sm font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider">TTL</label>
                <p class="text-2xl font-black text-gray-900 dark:text-white mt-2">
                  {{ formatTTL(data.ttl) }}
                </p>
              </div>
            </div>
          </div>
        </div>
        
        <!-- Partitions Tab -->
        <div v-if="activeTab === 'partitions'">
          <DataTable
            :columns="partitionColumns"
            v-model:search="partitionSearch"
            search-placeholder="Search partitions..."
            :total-items="filteredPartitions.length"
            :has-data="filteredPartitions.length > 0"
            empty-message="No partitions found"
            @sort="sortPartitions"
          >
            <tr
              v-for="partition in paginatedPartitions"
              :key="partition.id"
              class="hover:bg-gray-50 dark:hover:bg-gray-800/50"
            >
              <td class="px-6 py-4 whitespace-nowrap">
                <span class="text-sm font-bold text-gray-900 dark:text-white">
                  {{ partition.id }}
                </span>
              </td>
              <td class="px-6 py-4 whitespace-nowrap">
                <span class="text-sm text-gray-900 dark:text-white">
                  {{ partition.pending || 0 }}
                </span>
              </td>
              <td class="px-6 py-4 whitespace-nowrap">
                <span class="text-sm text-gray-900 dark:text-white">
                  {{ partition.processing || 0 }}
                </span>
              </td>
              <td class="px-6 py-4 whitespace-nowrap">
                <span class="text-sm text-gray-900 dark:text-white">
                  {{ partition.completed || 0 }}
                </span>
              </td>
              <td class="px-6 py-4 whitespace-nowrap">
                <span class="text-sm text-gray-900 dark:text-white">
                  {{ partition.failed || 0 }}
                </span>
              </td>
              <td class="px-6 py-4 whitespace-nowrap">
                <span
                  :class="[
                    'inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-bold',
                    partition.hasActiveLease
                      ? 'bg-emerald-100 dark:bg-emerald-900/30 text-emerald-800 dark:text-emerald-300'
                      : 'bg-gray-100 dark:bg-gray-800 text-gray-700 dark:text-gray-400'
                  ]"
                >
                  {{ partition.hasActiveLease ? 'Active' : 'Idle' }}
                </span>
              </td>
            </tr>
          </DataTable>
        </div>
      </div>
    </LoadingState>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted, h } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import AppLayout from '../components/layout/AppLayout.vue';
import MetricCardV2 from '../components/common/MetricCardV2.vue';
import LoadingState from '../components/common/LoadingState.vue';
import TabNavigation from '../components/common/TabNavigation.vue';
import StatsBar from '../components/common/StatsBar.vue';
import DataTable from '../components/common/DataTable.vue';
import QuickActions from '../components/common/QuickActions.vue';
import { useApi } from '../composables/useApi';

const route = useRoute();
const router = useRouter();
const queueName = route.params.queueName;

const RefreshIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15' })])};
const MessagesIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M8 10h.01M12 10h.01M16 10h.01M9 16H5a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v8a2 2 0 01-2 2h-5l-5 5v-5z' })])};

const { loading, error, execute, client } = useApi();
const data = ref(null);
const activeTab = ref('overview');
const partitionSearch = ref('');

const tabs = computed(() => [
  { id: 'overview', label: 'Overview' },
  { id: 'configuration', label: 'Configuration' },
  { id: 'partitions', label: 'Partitions', badge: data.value?.partitions?.length || 0 }
]);

const statsBarData = computed(() => [
  { label: 'Total Messages', value: formatNumber(data.value?.totalMessages || 0) },
  { label: 'Partitions', value: data.value?.partitions?.length || 0 },
  { label: 'Lease Time', value: data.value?.leaseTime ? `${data.value.leaseTime}s` : 'N/A' },
  { label: 'Retry Limit', value: data.value?.retryLimit || 'N/A' }
]);

const quickActions = computed(() => [
  {
    id: 'refresh',
    label: 'Refresh',
    icon: RefreshIcon,
    variant: 'secondary',
    onClick: fetchQueueDetail,
    shortcut: 'R'
  },
  {
    id: 'messages',
    label: 'View Messages',
    icon: MessagesIcon,
    variant: 'primary',
    onClick: () => router.push(`/queues/${queueName}/messages`)
  }
]);

const partitionColumns = [
  { key: 'id', label: 'Partition', sortable: true },
  { key: 'pending', label: 'Pending', sortable: true },
  { key: 'processing', label: 'Processing', sortable: true },
  { key: 'completed', label: 'Completed', sortable: true },
  { key: 'failed', label: 'Failed', sortable: true },
  { key: 'status', label: 'Status', sortable: false }
];

const fetchQueueDetail = async () => {
  try {
    const result = await execute(client.getQueueDetail.bind(client), queueName);
    if (result) {
      // Map API response structure properly (from original QueueDetail.vue)
      data.value = {
        name: result.queue?.name || queueName,
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
          id: p.name || p.id || p.partition,
          pending: p.messages?.pending || 0,
          processing: p.messages?.processing || 0,
          completed: p.messages?.completed || 0,
          failed: p.messages?.failed || 0,
          lag: p.lag?.seconds || 0,
          lagFormatted: p.lag?.formatted || 'N/A',
          hasActiveLease: p.hasActiveLease || false
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
  name: queueName,
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

const filteredPartitions = computed(() => {
  if (!data.value?.partitions) return [];
  if (!partitionSearch.value) return data.value.partitions;
  
  const query = partitionSearch.value.toLowerCase();
  return data.value.partitions.filter(p => 
    String(p.id).includes(query)
  );
});

const paginatedPartitions = computed(() => filteredPartitions.value);

const sortPartitions = (key) => {
  // Implement sorting if needed
  console.log('Sort by:', key);
};

const formatNumber = (num) => {
  if (num >= 1000000) return (num / 1000000).toFixed(1) + 'M';
  if (num >= 10000) return (num / 1000).toFixed(1) + 'K';
  return num.toLocaleString();
};

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

onMounted(async () => {
  await fetchQueueDetail();
});
</script>

