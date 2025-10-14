<template>
  <AppLayout>
    <div class="space-y-6">
      <!-- Filters -->
      <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-4">
        <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
          <!-- Search -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Search
            </label>
            <input
              v-model="searchQuery"
              type="text"
              placeholder="Search queues..."
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-green-500 focus:border-transparent"
            />
          </div>
          
          <!-- Status Filter -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Status
            </label>
            <select
              v-model="statusFilter"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-green-500 focus:border-transparent"
            >
              <option value="all">All</option>
              <option value="active">Active</option>
              <option value="idle">Idle</option>
            </select>
          </div>
          
          <!-- Sort By -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Sort By
            </label>
            <select
              v-model="sortBy"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-green-500 focus:border-transparent"
            >
              <option value="name">Name</option>
              <option value="pending">Pending Messages</option>
              <option value="processing">Processing</option>
              <option value="failed">Failed</option>
            </select>
          </div>
        </div>
      </div>
      
      <!-- Queues Table -->
      <LoadingState :loading="loading" :error="error" @retry="fetchQueues">
        <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 overflow-hidden">
          <div class="overflow-x-auto">
            <table class="w-full">
              <thead class="bg-gray-50 dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700">
                <tr>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Queue Name
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Partitions
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
                    Status
                  </th>
                  <th class="px-6 py-3 text-right text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Actions
                  </th>
                </tr>
              </thead>
              <tbody class="divide-y divide-gray-200 dark:divide-gray-700">
                <tr 
                  v-for="queue in filteredQueues" 
                  :key="queue.name"
                  class="hover:bg-gray-50 dark:hover:bg-gray-800 cursor-pointer transition-colors"
                  @click="$router.push(`/queues/${queue.name}`)"
                >
                  <td class="px-6 py-4 whitespace-nowrap">
                    <div class="flex items-center">
                      <div>
                        <div class="text-sm font-medium text-gray-900 dark:text-white">
                          {{ queue.name }}
                        </div>
                        <div v-if="queue.namespace" class="text-sm text-gray-500 dark:text-gray-400">
                          {{ queue.namespace }}
                        </div>
                      </div>
                    </div>
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ queue.partitions || 0 }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    <span :class="{ 'text-yellow-600 dark:text-yellow-400 font-semibold': (queue.pendingMessages || 0) > 100 }">
                      {{ (queue.pendingMessages || 0).toLocaleString() }}
                    </span>
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ (queue.processingMessages || 0).toLocaleString() }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ (queue.completedMessages || 0).toLocaleString() }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm">
                    <span :class="{ 'text-red-600 dark:text-red-400 font-semibold': (queue.failedMessages || 0) > 0 }">
                      {{ (queue.failedMessages || 0).toLocaleString() }}
                    </span>
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap">
                    <StatusBadge :status="getQueueStatus(queue)" />
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-right text-sm font-medium">
                    <button
                      @click.stop="$router.push(`/queues/${queue.name}/messages`)"
                      class="text-green-600 dark:text-green-400 hover:text-green-800 dark:hover:text-green-300"
                    >
                      View Messages
                    </button>
                  </td>
                </tr>
              </tbody>
            </table>
            
            <div v-if="!filteredQueues || filteredQueues.length === 0" class="text-center py-12">
              <p class="text-gray-500 dark:text-gray-400">No queues found</p>
            </div>
          </div>
        </div>
      </LoadingState>
    </div>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import AppLayout from '../components/layout/AppLayout.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingState from '../components/common/LoadingState.vue';
import { useApi } from '../composables/useApi';
import { usePolling } from '../composables/usePolling';

const { loading, error, execute, client } = useApi();
const queues = ref([]);
const searchQuery = ref('');
const statusFilter = ref('all');
const sortBy = ref('name');

const fetchQueues = async () => {
  try {
    const result = await execute(client.getQueues.bind(client));
    queues.value = result.queues || [];
  } catch (err) {
    console.error('Failed to fetch queues:', err);
    queues.value = [];
  }
};

const { startPolling, stopPolling } = usePolling(fetchQueues, 10000);

const filteredQueues = computed(() => {
  let filtered = [...queues.value];
  
  // Apply search filter
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase();
    filtered = filtered.filter(q => 
      q.name.toLowerCase().includes(query) ||
      (q.namespace && q.namespace.toLowerCase().includes(query))
    );
  }
  
  // Apply status filter
  if (statusFilter.value !== 'all') {
    filtered = filtered.filter(q => {
      const status = getQueueStatus(q);
      return status === statusFilter.value;
    });
  }
  
  // Apply sorting
  filtered.sort((a, b) => {
    switch (sortBy.value) {
      case 'name':
        return a.name.localeCompare(b.name);
      case 'pending':
        return (b.pendingMessages || 0) - (a.pendingMessages || 0);
      case 'processing':
        return (b.processingMessages || 0) - (a.processingMessages || 0);
      case 'failed':
        return (b.failedMessages || 0) - (a.failedMessages || 0);
      default:
        return 0;
    }
  });
  
  return filtered;
});

const getQueueStatus = (queue) => {
  const pending = queue.pendingMessages || 0;
  const processing = queue.processingMessages || 0;
  
  if (pending > 0 || processing > 0) {
    return 'active';
  }
  return 'inactive';
};

onMounted(async () => {
  await fetchQueues();
  // Don't auto-refresh by default
});

onUnmounted(() => {
  stopPolling();
});
</script>

