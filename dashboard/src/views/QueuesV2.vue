<template>
  <AppLayout>
    <div class="space-y-6">
      <!-- Header -->
      <div class="flex items-center justify-between">
        <div>
          <h1 class="text-3xl font-black text-gray-900 dark:text-white mb-1">
            Queues
          </h1>
          <p class="text-sm text-gray-600 dark:text-gray-400">
            Manage and monitor your message queues
          </p>
        </div>
        
        <QuickActions :actions="quickActions" />
      </div>
      
      <!-- Filters -->
      <div class="gradient-glow bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl">
        <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
          <!-- Search -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-2">
              Search
            </label>
            <div class="relative">
              <input
                v-model="searchQuery"
                type="text"
                placeholder="Search queues..."
                class="w-full pl-10 pr-4 py-2.5 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
              />
              <svg class="absolute left-3 top-3 w-5 h-5 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
              </svg>
            </div>
          </div>
          
          <!-- Status Filter -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-2">
              Status
            </label>
            <select
              v-model="statusFilter"
              class="w-full px-4 py-2.5 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
            >
              <option value="all">All Statuses</option>
              <option value="active">Active</option>
              <option value="inactive">Inactive</option>
            </select>
          </div>
          
          <!-- Sort By -->
          <div>
            <label class="block text-sm font-bold text-gray-700 dark:text-gray-300 mb-2">
              Sort By
            </label>
            <select
              v-model="sortBy"
              class="w-full px-4 py-2.5 rounded-xl border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white focus:ring-2 focus:ring-primary-500 focus:border-transparent transition-all"
            >
              <option value="name">Name</option>
              <option value="pending">Pending Messages</option>
              <option value="processing">Processing</option>
              <option value="failed">Failed</option>
            </select>
          </div>
        </div>
      </div>
      
      <!-- Queues Grid -->
      <LoadingState :loading="loading" :error="error" @retry="fetchQueues">
        <div v-if="filteredQueues.length > 0" class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          <div
            v-for="queue in filteredQueues"
            :key="queue.name"
            class="gradient-glow group bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-2xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-xl hover:shadow-2xl cursor-pointer"
            @click="$router.push(`/queues/${queue.name}`)"
          >
            <!-- Queue Header -->
            <div class="flex items-start justify-between mb-4">
              <div class="flex items-center gap-3 flex-1 min-w-0">
                <div class="flex-shrink-0 w-12 h-12 rounded-xl bg-gradient-to-br from-primary-500 to-blue-600 flex items-center justify-center text-white font-bold text-lg shadow-lg shadow-primary-500/30">
                  {{ queue.name.substring(0, 2).toUpperCase() }}
                </div>
                <div class="flex-1 min-w-0">
                  <h3 class="text-lg font-black text-gray-900 dark:text-white truncate group-hover:text-primary-600 dark:group-hover:text-primary-400 transition-colors">
                    {{ queue.name }}
                  </h3>
                  <p class="text-sm text-gray-500 dark:text-gray-400">
                    {{ queue.partitions || 0 }} partition{{ queue.partitions !== 1 ? 's' : '' }}
                  </p>
                </div>
              </div>
              <StatusBadge :status="getQueueStatus(queue)" />
            </div>
            
            <!-- Metrics Grid -->
            <div class="grid grid-cols-2 gap-3 mb-4">
              <div class="p-3 bg-gradient-to-br from-amber-50 to-orange-50/50 dark:from-amber-900/20 dark:to-orange-900/10 rounded-xl border border-amber-100 dark:border-amber-800/30">
                <div class="text-xs font-bold text-amber-600 dark:text-amber-400 uppercase tracking-wider mb-1">Pending</div>
                <div class="text-2xl font-black text-amber-900 dark:text-amber-100 metric-value">
                  {{ formatNumber(queue.pendingMessages || 0) }}
                </div>
              </div>
              
              <div class="p-3 bg-gradient-to-br from-blue-50 to-cyan-50/50 dark:from-blue-900/20 dark:to-cyan-900/10 rounded-xl border border-blue-100 dark:border-blue-800/30">
                <div class="text-xs font-bold text-blue-600 dark:text-blue-400 uppercase tracking-wider mb-1">Processing</div>
                <div class="text-2xl font-black text-blue-900 dark:text-blue-100 metric-value">
                  {{ formatNumber(queue.processingMessages || 0) }}
                </div>
              </div>
              
              <div class="p-3 bg-gradient-to-br from-emerald-50 to-teal-50/50 dark:from-emerald-900/20 dark:to-teal-900/10 rounded-xl border border-emerald-100 dark:border-emerald-800/30">
                <div class="text-xs font-bold text-emerald-600 dark:text-emerald-400 uppercase tracking-wider mb-1">Completed</div>
                <div class="text-2xl font-black text-emerald-900 dark:text-emerald-100 metric-value">
                  {{ formatNumber(queue.completedMessages || 0) }}
                </div>
              </div>
              
              <div class="p-3 bg-gradient-to-br from-red-50 to-rose-50/50 dark:from-red-900/20 dark:to-rose-900/10 rounded-xl border border-red-100 dark:border-red-800/30">
                <div class="text-xs font-bold text-red-600 dark:text-red-400 uppercase tracking-wider mb-1">Failed</div>
                <div class="text-2xl font-black text-red-900 dark:text-red-100 metric-value">
                  {{ formatNumber(queue.failedMessages || 0) }}
                </div>
              </div>
            </div>
            
            <!-- Actions -->
            <div class="flex items-center gap-2">
              <button
                @click.stop="$router.push(`/queues/${queue.name}/messages`)"
                class="flex-1 px-4 py-2 bg-primary-600 hover:bg-primary-700 text-white rounded-xl text-sm font-semibold"
              >
                View Messages
              </button>
              <button
                @click.stop="$router.push(`/queues/${queue.name}`)"
                class="p-2 hover:bg-gray-100 dark:hover:bg-gray-800 rounded-xl transition-colors"
                title="Details"
              >
                <svg class="w-5 h-5 text-gray-600 dark:text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
              </button>
            </div>
          </div>
        </div>
        
        <!-- Empty State -->
        <div v-else class="text-center py-16">
          <div class="inline-flex items-center justify-center w-20 h-20 bg-gray-100 dark:bg-gray-800 rounded-full mb-4">
            <svg class="w-10 h-10 text-gray-400 dark:text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
            </svg>
          </div>
          <h3 class="text-xl font-bold text-gray-900 dark:text-white mb-2">No queues found</h3>
          <p class="text-gray-600 dark:text-gray-400 mb-6">
            {{ searchQuery ? 'Try adjusting your search or filters' : 'Create your first queue to get started' }}
          </p>
        </div>
      </LoadingState>
    </div>
  </AppLayout>
</template>

<script setup>
import { ref, computed, onMounted, h } from 'vue';
import AppLayout from '../components/layout/AppLayout.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingState from '../components/common/LoadingState.vue';
import QuickActions from '../components/common/QuickActions.vue';
import { useApi } from '../composables/useApi';

const RefreshIcon = { render: () => h('svg', { fill: 'none', stroke: 'currentColor', viewBox: '0 0 24 24' }, [h('path', { strokeLinecap: 'round', strokeLinejoin: 'round', strokeWidth: '2', d: 'M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15' })])};

const { loading, error, execute, client } = useApi();
const queues = ref([]);
const searchQuery = ref('');
const statusFilter = ref('all');
const sortBy = ref('name');

const quickActions = computed(() => [
  {
    id: 'refresh',
    label: 'Refresh',
    icon: RefreshIcon,
    variant: 'secondary',
    onClick: fetchQueues,
    shortcut: 'R'
  }
]);

const fetchQueues = async () => {
  try {
    const result = await execute(client.getQueues.bind(client));
    queues.value = (result.queues || []).map(q => ({
      ...q,
      pendingMessages: q.messages?.pending || 0,
      processingMessages: q.messages?.processing || 0,
      completedMessages: q.messages?.completed || 0,
      failedMessages: q.messages?.failed || 0,
      totalMessages: q.messages?.total || 0
    }));
  } catch (err) {
    console.error('Failed to fetch queues:', err);
    queues.value = [];
  }
};

const filteredQueues = computed(() => {
  let filtered = [...queues.value];
  
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase();
    filtered = filtered.filter(q => 
      q.name.toLowerCase().includes(query) ||
      (q.namespace && q.namespace.toLowerCase().includes(query))
    );
  }
  
  if (statusFilter.value !== 'all') {
    filtered = filtered.filter(q => getQueueStatus(q) === statusFilter.value);
  }
  
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
  return (pending > 0 || processing > 0) ? 'active' : 'inactive';
};

const formatNumber = (num) => {
  if (num >= 1000000) return (num / 1000000).toFixed(1) + 'M';
  if (num >= 10000) return (num / 1000).toFixed(1) + 'K';
  return num.toLocaleString();
};

onMounted(async () => {
  await fetchQueues();
});
</script>

