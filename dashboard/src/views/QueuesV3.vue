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
            {{ filteredQueues.length }} of {{ queues.length }} queues
          </p>
        </div>
        
        <div class="flex items-center gap-3">
          <!-- View Toggle -->
          <div class="flex items-center gap-1 bg-gray-200 dark:bg-gray-800 rounded-lg p-1">
            <button
              @click="viewMode = 'table'"
              :class="[
                'px-3 py-1.5 text-xs font-bold rounded-md',
                viewMode === 'table'
                  ? 'bg-white dark:bg-gray-900 text-gray-900 dark:text-white shadow-sm'
                  : 'text-gray-600 dark:text-gray-400'
              ]"
            >
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M3 10h18M3 14h18m-9-4v8m-7 0h14a2 2 0 002-2V8a2 2 0 00-2-2H5a2 2 0 00-2 2v8a2 2 0 002 2z" />
              </svg>
            </button>
            <button
              @click="viewMode = 'cards'"
              :class="[
                'px-3 py-1.5 text-xs font-bold rounded-md',
                viewMode === 'cards'
                  ? 'bg-white dark:bg-gray-900 text-gray-900 dark:text-white shadow-sm'
                  : 'text-gray-600 dark:text-gray-400'
              ]"
            >
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M4 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2V6zM14 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2V6zM4 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2v-2zM14 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2v-2z" />
              </svg>
            </button>
          </div>
          
          <QuickActions :actions="quickActions" />
        </div>
      </div>
      
      <!-- Stats Bar -->
      <div class="flex items-center gap-6 flex-wrap">
        <div class="flex items-center gap-2">
          <span class="text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider">Total Queues</span>
          <span class="px-2.5 py-1 bg-gray-200 dark:bg-gray-800 rounded text-sm font-bold text-gray-900 dark:text-white">
            {{ queues.length }}
          </span>
        </div>
        <div class="flex items-center gap-2">
          <span class="text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider">Active</span>
          <span class="px-2.5 py-1 bg-emerald-100 dark:bg-emerald-900/30 rounded text-sm font-bold text-emerald-800 dark:text-emerald-300">
            {{ activeCount }}
          </span>
        </div>
        <div class="flex items-center gap-2">
          <span class="text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider">Total Pending</span>
          <span class="px-2.5 py-1 bg-amber-100 dark:bg-amber-900/30 rounded text-sm font-bold text-amber-800 dark:text-amber-300">
            {{ formatNumber(totalPending) }}
          </span>
        </div>
        <div class="flex items-center gap-2">
          <span class="text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider">Total Completed</span>
          <span class="px-2.5 py-1 bg-blue-100 dark:bg-blue-900/30 rounded text-sm font-bold text-blue-800 dark:text-blue-300">
            {{ formatNumber(totalCompleted) }}
          </span>
        </div>
      </div>
      
      <LoadingState :loading="loading" :error="error" @retry="fetchQueues">
        <!-- Table View -->
        <div v-if="viewMode === 'table'">
          <div class="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 overflow-hidden shadow-lg">
            <!-- Search and Filters -->
            <div class="p-4 border-b border-gray-200 dark:border-gray-800 bg-gray-50 dark:bg-gray-800/50">
              <div class="grid grid-cols-1 md:grid-cols-4 gap-4">
                <!-- Search -->
                <div class="md:col-span-2">
                  <div class="relative">
                    <input
                      v-model="searchQuery"
                      type="text"
                      placeholder="Search queues by name or namespace..."
                      class="w-full pl-10 pr-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white text-sm focus:ring-2 focus:ring-primary-500 focus:border-transparent"
                    />
                    <svg class="absolute left-3 top-2.5 w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
                    </svg>
                  </div>
                </div>
                
                <!-- Status Filter -->
                <div>
                  <select
                    v-model="statusFilter"
                    class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white text-sm focus:ring-2 focus:ring-primary-500 focus:border-transparent"
                  >
                    <option value="all">All Statuses</option>
                    <option value="active">Active</option>
                    <option value="inactive">Inactive</option>
                  </select>
                </div>
                
                <!-- Sort By -->
                <div>
                  <select
                    v-model="sortBy"
                    class="w-full px-4 py-2 rounded-lg border border-gray-300 dark:border-gray-700 bg-white dark:bg-gray-800 text-gray-900 dark:text-white text-sm focus:ring-2 focus:ring-primary-500 focus:border-transparent"
                  >
                    <option value="name">Name</option>
                    <option value="pending">Pending (High to Low)</option>
                    <option value="completed">Completed (High to Low)</option>
                    <option value="failed">Failed (High to Low)</option>
                  </select>
                </div>
              </div>
            </div>
            
            <!-- Table -->
            <div class="overflow-x-auto">
              <table class="w-full">
                <thead class="bg-gray-50 dark:bg-gray-800/50 sticky top-0">
                  <tr>
                    <th 
                      class="px-6 py-3 text-left text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider cursor-pointer hover:text-gray-900 dark:hover:text-gray-200"
                      @click="toggleSort('name')"
                    >
                      <div class="flex items-center gap-2">
                        Queue Name
                        <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M8 9l4-4 4 4m0 6l-4 4-4-4" />
                        </svg>
                      </div>
                    </th>
                    <th class="px-6 py-3 text-left text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider">
                      Namespace
                    </th>
                    <th 
                      class="px-6 py-3 text-left text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider cursor-pointer hover:text-gray-900 dark:hover:text-gray-200"
                      @click="toggleSort('partitions')"
                    >
                      Partitions
                    </th>
                    <th 
                      class="px-6 py-3 text-right text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider cursor-pointer hover:text-gray-900 dark:hover:text-gray-200"
                      @click="toggleSort('pending')"
                    >
                      Pending
                    </th>
                    <th class="px-6 py-3 text-right text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider">
                      Processing
                    </th>
                    <th 
                      class="px-6 py-3 text-right text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider cursor-pointer hover:text-gray-900 dark:hover:text-gray-200"
                      @click="toggleSort('completed')"
                    >
                      Completed
                    </th>
                    <th 
                      class="px-6 py-3 text-right text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider cursor-pointer hover:text-gray-900 dark:hover:text-gray-200"
                      @click="toggleSort('failed')"
                    >
                      Failed
                    </th>
                    <th class="px-6 py-3 text-center text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider">
                      Status
                    </th>
                    <th class="px-6 py-3 text-right text-xs font-bold text-gray-600 dark:text-gray-400 uppercase tracking-wider">
                      Actions
                    </th>
                  </tr>
                </thead>
                <tbody class="divide-y divide-gray-200 dark:divide-gray-800">
                  <tr 
                    v-for="queue in paginatedQueues" 
                    :key="queue.name"
                    class="hover:bg-gray-50 dark:hover:bg-gray-800/50 cursor-pointer"
                    @click="$router.push(`/queues/${queue.name}`)"
                  >
                    <td class="px-6 py-4 whitespace-nowrap">
                      <div class="flex items-center gap-3">
                        <div class="flex-shrink-0 w-8 h-8 rounded-lg bg-gradient-to-br from-primary-500 to-blue-600 flex items-center justify-center text-white text-xs font-bold shadow-sm">
                          {{ queue.name.substring(0, 2).toUpperCase() }}
                        </div>
                        <div class="text-sm font-bold text-gray-900 dark:text-white">
                          {{ queue.name }}
                        </div>
                      </div>
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-600 dark:text-gray-400">
                      {{ queue.namespace || '-' }}
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-white">
                      {{ queue.partitions || 0 }}
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-right">
                      <span :class="[
                        'text-sm font-bold',
                        (queue.pendingMessages || 0) > 0 
                          ? 'text-amber-600 dark:text-amber-400' 
                          : 'text-gray-900 dark:text-white'
                      ]">
                        {{ formatNumber(queue.pendingMessages || 0) }}
                      </span>
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-right text-sm font-medium text-gray-900 dark:text-white">
                      {{ formatNumber(queue.processingMessages || 0) }}
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-right">
                      <span class="text-sm font-bold text-emerald-600 dark:text-emerald-400">
                        {{ formatNumber(queue.completedMessages || 0) }}
                      </span>
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-right">
                      <span :class="[
                        'text-sm font-bold',
                        (queue.failedMessages || 0) > 0 
                          ? 'text-red-600 dark:text-red-400' 
                          : 'text-gray-900 dark:text-white'
                      ]">
                        {{ formatNumber(queue.failedMessages || 0) }}
                      </span>
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-center">
                      <StatusBadge :status="getQueueStatus(queue)" />
                    </td>
                    <td class="px-6 py-4 whitespace-nowrap text-right">
                      <button
                        @click.stop="$router.push(`/queues/${queue.name}/messages`)"
                        class="text-xs font-semibold text-primary-600 dark:text-primary-400 hover:text-primary-800 dark:hover:text-primary-300"
                      >
                        View →
                      </button>
                    </td>
                  </tr>
                </tbody>
              </table>
              
              <!-- Empty State -->
              <div v-if="filteredQueues.length === 0" class="text-center py-12">
                <svg class="w-12 h-12 mx-auto text-gray-300 dark:text-gray-700 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                </svg>
                <p class="text-sm font-medium text-gray-500 dark:text-gray-400">
                  {{ searchQuery ? 'No queues match your search' : 'No queues found' }}
                </p>
              </div>
            </div>
            
            <!-- Pagination -->
            <div v-if="totalPages > 1" class="px-6 py-4 border-t border-gray-200 dark:border-gray-800 flex items-center justify-between bg-gray-50 dark:bg-gray-800/50">
              <div class="text-sm text-gray-600 dark:text-gray-400">
                Showing {{ startIndex }} - {{ endIndex }} of {{ filteredQueues.length }}
              </div>
              <div class="flex items-center gap-2">
                <button
                  @click="currentPage--"
                  :disabled="currentPage === 1"
                  class="p-2 rounded-lg hover:bg-gray-200 dark:hover:bg-gray-700 disabled:opacity-30 disabled:cursor-not-allowed"
                >
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M15 19l-7-7 7-7" />
                  </svg>
                </button>
                <span class="text-sm font-medium text-gray-900 dark:text-white">
                  Page {{ currentPage }} of {{ totalPages }}
                </span>
                <button
                  @click="currentPage++"
                  :disabled="currentPage === totalPages"
                  class="p-2 rounded-lg hover:bg-gray-200 dark:hover:bg-gray-700 disabled:opacity-30 disabled:cursor-not-allowed"
                >
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M9 5l7 7-7 7" />
                  </svg>
                </button>
              </div>
            </div>
          </div>
        </div>
        
        <!-- Card View -->
        <div v-else class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          <div
            v-for="queue in paginatedQueues"
            :key="queue.name"
            class="group bg-gradient-to-br from-white to-gray-50/50 dark:from-gray-900 dark:to-gray-900/50 rounded-xl border border-gray-200/80 dark:border-gray-800/80 p-6 shadow-lg hover:shadow-xl cursor-pointer"
            @click="$router.push(`/queues/${queue.name}`)"
          >
            <!-- Queue Header -->
            <div class="flex items-start justify-between mb-4">
              <div class="flex items-center gap-3 flex-1 min-w-0">
                <div class="flex-shrink-0 w-12 h-12 rounded-xl bg-gradient-to-br from-primary-500 to-blue-600 flex items-center justify-center text-white font-bold text-lg shadow-lg shadow-primary-500/30">
                  {{ queue.name.substring(0, 2).toUpperCase() }}
                </div>
                <div class="flex-1 min-w-0">
                  <h3 class="text-lg font-black text-gray-900 dark:text-white truncate group-hover:text-primary-600 dark:group-hover:text-primary-400">
                    {{ queue.name }}
                  </h3>
                  <p class="text-sm text-gray-500 dark:text-gray-400">
                    {{ queue.namespace || 'default' }} • {{ queue.partitions || 0 }} partitions
                  </p>
                </div>
              </div>
              <StatusBadge :status="getQueueStatus(queue)" />
            </div>
            
            <!-- Metrics Grid -->
            <div class="grid grid-cols-2 gap-3 mb-4">
              <div class="p-3 bg-gradient-to-br from-amber-50 to-orange-50/50 dark:from-amber-900/20 dark:to-orange-900/10 rounded-lg border border-amber-100 dark:border-amber-800/30">
                <div class="text-xs font-bold text-amber-600 dark:text-amber-400 uppercase tracking-wider mb-1">Pending</div>
                <div class="text-xl font-black text-amber-900 dark:text-amber-100 metric-value">
                  {{ formatNumber(queue.pendingMessages || 0) }}
                </div>
              </div>
              
              <div class="p-3 bg-gradient-to-br from-blue-50 to-cyan-50/50 dark:from-blue-900/20 dark:to-cyan-900/10 rounded-lg border border-blue-100 dark:border-blue-800/30">
                <div class="text-xs font-bold text-blue-600 dark:text-blue-400 uppercase tracking-wider mb-1">Processing</div>
                <div class="text-xl font-black text-blue-900 dark:text-blue-100 metric-value">
                  {{ formatNumber(queue.processingMessages || 0) }}
                </div>
              </div>
              
              <div class="p-3 bg-gradient-to-br from-emerald-50 to-teal-50/50 dark:from-emerald-900/20 dark:to-teal-900/10 rounded-lg border border-emerald-100 dark:border-emerald-800/30">
                <div class="text-xs font-bold text-emerald-600 dark:text-emerald-400 uppercase tracking-wider mb-1">Completed</div>
                <div class="text-xl font-black text-emerald-900 dark:text-emerald-100 metric-value">
                  {{ formatNumber(queue.completedMessages || 0) }}
                </div>
              </div>
              
              <div class="p-3 bg-gradient-to-br from-red-50 to-rose-50/50 dark:from-red-900/20 dark:to-rose-900/10 rounded-lg border border-red-100 dark:border-red-800/30">
                <div class="text-xs font-bold text-red-600 dark:text-red-400 uppercase tracking-wider mb-1">Failed</div>
                <div class="text-xl font-black text-red-900 dark:text-red-100 metric-value">
                  {{ formatNumber(queue.failedMessages || 0) }}
                </div>
              </div>
            </div>
            
            <!-- Actions -->
            <div class="flex items-center gap-2">
              <button
                @click.stop="$router.push(`/queues/${queue.name}/messages`)"
                class="flex-1 px-4 py-2 bg-primary-600 hover:bg-primary-700 text-white rounded-lg text-sm font-semibold"
              >
                View Messages
              </button>
              <button
                @click.stop="$router.push(`/queues/${queue.name}`)"
                class="p-2 hover:bg-gray-100 dark:hover:bg-gray-800 rounded-lg"
                title="Details"
              >
                <svg class="w-5 h-5 text-gray-600 dark:text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
              </button>
            </div>
          </div>
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
const sortDirection = ref('asc');
const viewMode = ref('table'); // 'table' or 'cards'
const currentPage = ref(1);
const pageSize = ref(25);

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
    const result = await execute(client.getQueues.bind(client), { limit: 1000 });
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
  
  // Search filter
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase();
    filtered = filtered.filter(q => 
      q.name.toLowerCase().includes(query) ||
      (q.namespace && q.namespace.toLowerCase().includes(query))
    );
  }
  
  // Status filter
  if (statusFilter.value !== 'all') {
    filtered = filtered.filter(q => getQueueStatus(q) === statusFilter.value);
  }
  
  // Sorting
  filtered.sort((a, b) => {
    let aVal, bVal;
    
    switch (sortBy.value) {
      case 'name':
        return a.name.localeCompare(b.name) * (sortDirection.value === 'asc' ? 1 : -1);
      case 'pending':
        return ((b.pendingMessages || 0) - (a.pendingMessages || 0)) * (sortDirection.value === 'asc' ? -1 : 1);
      case 'completed':
        return ((b.completedMessages || 0) - (a.completedMessages || 0)) * (sortDirection.value === 'asc' ? -1 : 1);
      case 'failed':
        return ((b.failedMessages || 0) - (a.failedMessages || 0)) * (sortDirection.value === 'asc' ? -1 : 1);
      case 'partitions':
        return ((b.partitions || 0) - (a.partitions || 0)) * (sortDirection.value === 'asc' ? -1 : 1);
      default:
        return 0;
    }
  });
  
  return filtered;
});

const paginatedQueues = computed(() => {
  const start = (currentPage.value - 1) * pageSize.value;
  const end = start + pageSize.value;
  return filteredQueues.value.slice(start, end);
});

const totalPages = computed(() => Math.ceil(filteredQueues.value.length / pageSize.value));
const startIndex = computed(() => (currentPage.value - 1) * pageSize.value + 1);
const endIndex = computed(() => Math.min(currentPage.value * pageSize.value, filteredQueues.value.length));

const activeCount = computed(() => 
  queues.value.filter(q => getQueueStatus(q) === 'active').length
);

const totalPending = computed(() => 
  queues.value.reduce((sum, q) => sum + (q.pendingMessages || 0), 0)
);

const totalCompleted = computed(() => 
  queues.value.reduce((sum, q) => sum + (q.completedMessages || 0), 0)
);

const getQueueStatus = (queue) => {
  const pending = queue.pendingMessages || 0;
  const processing = queue.processingMessages || 0;
  return (pending > 0 || processing > 0) ? 'active' : 'inactive';
};

const toggleSort = (column) => {
  if (sortBy.value === column) {
    sortDirection.value = sortDirection.value === 'asc' ? 'desc' : 'asc';
  } else {
    sortBy.value = column;
    sortDirection.value = 'desc';
  }
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

