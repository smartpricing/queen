<template>
  <AppLayout>
    <div class="space-y-6">
      <!-- Header -->
      <div>
        <button 
          @click="$router.back()"
          class="text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-white mb-2 inline-flex items-center"
        >
          <svg class="w-5 h-5 mr-1" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
          </svg>
          Back
        </button>
        <h1 class="text-3xl font-bold text-gray-900 dark:text-white">
          Messages: {{ queueName }}
        </h1>
      </div>
      
      <!-- Filters -->
      <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 p-4">
        <div class="grid grid-cols-1 md:grid-cols-4 gap-4">
          <!-- Status Filter -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Status
            </label>
            <select
              v-model="statusFilter"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-emerald-500 focus:border-transparent"
            >
              <option value="all">All</option>
              <option value="pending">Pending</option>
              <option value="processing">Processing</option>
              <option value="completed">Completed</option>
              <option value="failed">Failed</option>
            </select>
          </div>
          
          <!-- Partition Filter -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Partition
            </label>
            <select
              v-model="partitionFilter"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-emerald-500 focus:border-transparent"
            >
              <option value="all">All</option>
              <option v-for="p in partitions" :key="p" :value="p">
                Partition {{ p }}
              </option>
            </select>
          </div>
          
          <!-- Limit -->
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Limit
            </label>
            <select
              v-model="limit"
              class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-emerald-500 focus:border-transparent"
            >
              <option :value="50">50</option>
              <option :value="100">100</option>
              <option :value="250">250</option>
              <option :value="500">500</option>
            </select>
          </div>
          
          <!-- Apply Filters Button -->
          <div class="flex items-end">
            <button
              @click="fetchMessages"
              class="w-full px-4 py-2 bg-emerald-600 hover:bg-emerald-700 text-white rounded-lg transition-colors"
            >
              Apply Filters
            </button>
          </div>
        </div>
      </div>
      
      <!-- Messages Table -->
      <LoadingState :loading="loading" :error="error" @retry="fetchMessages">
        <div class="bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 overflow-hidden">
          <div class="overflow-x-auto">
            <table class="w-full">
              <thead class="bg-gray-50 dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700">
                <tr>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Transaction ID
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Partition
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Status
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Created At
                  </th>
                  <th class="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Retries
                  </th>
                  <th class="px-6 py-3 text-right text-xs font-medium text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    Actions
                  </th>
                </tr>
              </thead>
              <tbody class="divide-y divide-gray-200 dark:divide-gray-700">
                <tr 
                  v-for="message in messages" 
                  :key="message.transactionId"
                  class="hover:bg-gray-50 dark:hover:bg-gray-800 cursor-pointer transition-colors"
                  @click="openMessageDetail(message)"
                >
                  <td class="px-6 py-4 whitespace-nowrap">
                    <div class="text-sm font-mono text-gray-900 dark:text-white">
                      {{ truncateId(message.transactionId) }}
                    </div>
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ message.partition }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap">
                    <StatusBadge :status="message.status" />
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ formatDate(message.createdAt) }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                    {{ message.retryCount || 0 }}
                  </td>
                  <td class="px-6 py-4 whitespace-nowrap text-right text-sm font-medium">
                    <button
                      @click.stop="copyToClipboard(message.transactionId)"
                      class="text-emerald-600 dark:text-emerald-400 hover:text-emerald-800 dark:hover:text-emerald-300"
                    >
                      Copy ID
                    </button>
                  </td>
                </tr>
              </tbody>
            </table>
            
            <div v-if="!messages || messages.length === 0" class="text-center py-12">
              <p class="text-gray-500 dark:text-gray-400">No messages found</p>
            </div>
          </div>
        </div>
      </LoadingState>
    </div>
    
    <!-- Message Detail Modal -->
    <div 
      v-if="selectedMessage"
      class="fixed inset-0 z-50 overflow-y-auto"
      @click.self="selectedMessage = null"
    >
      <div class="flex items-center justify-center min-h-screen px-4">
        <div class="fixed inset-0 bg-gray-900/50 backdrop-blur-sm" @click="selectedMessage = null"></div>
        
        <div class="relative bg-white dark:bg-gray-900 rounded-lg border border-gray-200 dark:border-gray-800 max-w-3xl w-full max-h-[90vh] overflow-hidden">
          <!-- Modal Header -->
          <div class="flex items-center justify-between p-6 border-b border-gray-200 dark:border-gray-800">
            <h2 class="text-xl font-semibold text-gray-900 dark:text-white">
              Message Details
            </h2>
            <button
              @click="selectedMessage = null"
              class="text-gray-400 hover:text-gray-600 dark:hover:text-gray-300"
            >
              <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
              </svg>
            </button>
          </div>
          
          <!-- Modal Body -->
          <div class="p-6 overflow-y-auto max-h-[calc(90vh-140px)] space-y-4">
            <div>
              <label class="text-sm font-medium text-gray-600 dark:text-gray-400">Transaction ID</label>
              <p class="mt-1 text-sm font-mono text-gray-900 dark:text-white">{{ selectedMessage.transactionId }}</p>
            </div>
            
            <div v-if="selectedMessage.traceId">
              <label class="text-sm font-medium text-gray-600 dark:text-gray-400">Trace ID</label>
              <p class="mt-1 text-sm font-mono text-gray-900 dark:text-white">{{ selectedMessage.traceId }}</p>
            </div>
            
            <div class="grid grid-cols-2 gap-4">
              <div>
                <label class="text-sm font-medium text-gray-600 dark:text-gray-400">Status</label>
                <div class="mt-1">
                  <StatusBadge :status="selectedMessage.status" />
                </div>
              </div>
              
              <div>
                <label class="text-sm font-medium text-gray-600 dark:text-gray-400">Partition</label>
                <p class="mt-1 text-sm text-gray-900 dark:text-white">{{ selectedMessage.partition }}</p>
              </div>
            </div>
            
            <div class="grid grid-cols-2 gap-4">
              <div>
                <label class="text-sm font-medium text-gray-600 dark:text-gray-400">Created At</label>
                <p class="mt-1 text-sm text-gray-900 dark:text-white">{{ formatDate(selectedMessage.createdAt, true) }}</p>
              </div>
              
              <div>
                <label class="text-sm font-medium text-gray-600 dark:text-gray-400">Retry Count</label>
                <p class="mt-1 text-sm text-gray-900 dark:text-white">{{ selectedMessage.retryCount || 0 }}</p>
              </div>
            </div>
            
            <div v-if="selectedMessage.payload">
              <label class="text-sm font-medium text-gray-600 dark:text-gray-400">Payload</label>
              <pre class="mt-1 p-4 bg-gray-50 dark:bg-gray-800 rounded-lg text-sm text-gray-900 dark:text-white overflow-x-auto">{{ JSON.stringify(selectedMessage.payload, null, 2) }}</pre>
            </div>
            
            <div v-if="selectedMessage.error">
              <label class="text-sm font-medium text-red-600 dark:text-red-400">Error</label>
              <pre class="mt-1 p-4 bg-red-50 dark:bg-red-900/20 rounded-lg text-sm text-red-900 dark:text-red-300 overflow-x-auto">{{ selectedMessage.error }}</pre>
            </div>
          </div>
        </div>
      </div>
    </div>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { useRoute } from 'vue-router';
import AppLayout from '../components/layout/AppLayout.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingState from '../components/common/LoadingState.vue';
import { useApi } from '../composables/useApi';

const route = useRoute();
const queueName = computed(() => route.params.queueName);

const { loading, error, execute, client } = useApi();
const messages = ref([]);
const selectedMessage = ref(null);
const statusFilter = ref('all');
const partitionFilter = ref('all');
const limit = ref(50);
const partitions = ref([0, 1, 2, 3]); // Default partitions

const fetchMessages = async () => {
  try {
    const params = {
      limit: limit.value
    };
    
    if (statusFilter.value !== 'all') {
      params.status = statusFilter.value;
    }
    
    if (partitionFilter.value !== 'all') {
      params.partition = partitionFilter.value;
    }
    
    const result = await execute(client.getQueueMessages.bind(client), queueName.value, params);
    messages.value = result.messages || [];
  } catch (err) {
    console.error('Failed to fetch messages:', err);
  }
};

const openMessageDetail = (message) => {
  selectedMessage.value = message;
};

const truncateId = (id) => {
  if (!id) return '';
  return id.length > 16 ? `${id.substring(0, 16)}...` : id;
};

const formatDate = (dateStr, full = false) => {
  if (!dateStr) return 'N/A';
  
  const date = new Date(dateStr);
  if (full) {
    return date.toLocaleString();
  }
  
  const now = new Date();
  const diff = now - date;
  const seconds = Math.floor(diff / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  const days = Math.floor(hours / 24);
  
  if (days > 0) return `${days}d ago`;
  if (hours > 0) return `${hours}h ago`;
  if (minutes > 0) return `${minutes}m ago`;
  return `${seconds}s ago`;
};

const copyToClipboard = async (text) => {
  try {
    await navigator.clipboard.writeText(text);
    // Could show a toast notification here
  } catch (err) {
    console.error('Failed to copy:', err);
  }
};

onMounted(() => {
  fetchMessages();
});
</script>

