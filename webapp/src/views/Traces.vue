<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <!-- Auto-refresh Control -->
        <div class="auto-refresh-banner">
          <div class="flex items-center gap-3">
            <button
              @click="autoRefresh.toggle()"
              class="auto-refresh-toggle"
              :class="{ 'active': autoRefresh.isEnabled.value }"
            >
              <svg v-if="autoRefresh.isEnabled.value" class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M5 13l4 4L19 7" />
              </svg>
              <svg v-else class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
              </svg>
              <span class="text-xs font-medium">Auto-refresh</span>
            </button>
            
            <div v-if="autoRefresh.lastRefreshAt.value" class="text-xs text-gray-500 dark:text-gray-400">
              <span>Last updated {{ autoRefresh.timeSinceLastRefresh.value }}</span>
            </div>
            
            <div v-if="autoRefresh.isRefreshing.value" class="flex items-center gap-1.5 text-xs text-rose-600 dark:text-rose-400">
              <svg class="w-3.5 h-3.5 animate-spin" fill="none" viewBox="0 0 24 24">
                <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
                <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
              </svg>
              <span>Refreshing...</span>
            </div>
          </div>
          
          <button
            @click="autoRefresh.refresh()"
            class="manual-refresh-button"
            :disabled="autoRefresh.isRefreshing.value"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
              <path stroke-linecap="round" stroke-linejoin="round" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
            <span class="text-xs font-medium">Refresh Now</span>
          </button>
        </div>

        <!-- Search Box -->
        <div class="filter-card">
          <div class="space-y-3">
            <div class="flex gap-3">
              <div class="flex-1">
                <input
                  v-model="searchTraceName"
                  @keyup.enter="searchTraces"
                  type="text"
                  placeholder="Enter trace name (e.g., tenant-acme, order-flow-123)"
                  class="w-full px-4 py-2.5 bg-white dark:bg-slate-800 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-rose-500 focus:border-transparent text-sm"
                />
              </div>
              <button
                @click="searchTraces"
                :disabled="!searchTraceName || loading"
                class="btn btn-primary px-6"
              >
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
                </svg>
                Search
              </button>
            </div>

            <!-- Quick Examples -->
            <div v-if="!currentTraceName" class="flex flex-wrap gap-2 text-xs">
              <span class="text-gray-500 dark:text-gray-400">Quick examples:</span>
              <button
                v-for="example in exampleTraceNames"
                :key="example"
                @click="searchTraceName = example; searchTraces()"
                class="px-2 py-1 bg-gray-100 dark:bg-slate-700 hover:bg-gray-200 dark:hover:bg-slate-600 rounded text-gray-700 dark:text-gray-300 transition-colors"
              >
                {{ example }}
              </button>
            </div>
          </div>
        </div>

        <LoadingSpinner v-if="loading" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading traces:</strong> {{ error }}</p>
        </div>

        <!-- Results -->
        <div v-else-if="currentTraceName && traces.length > 0" class="space-y-4">
          <!-- Summary -->
          <div class="bg-orange-50 dark:bg-orange-900/20 border border-emerald-200 dark:border-orange-700/30 rounded-lg p-4">
            <div class="flex items-center gap-3">
              <svg class="w-5 h-5 text-orange-600 dark:text-pink-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <div class="flex-1">
                <p class="text-sm font-semibold text-orange-900 dark:text-orange-100">
                  Found {{ totalTraces }} trace{{ totalTraces !== 1 ? 's' : '' }} for: <span>{{ currentTraceName }}</span>
                </p>
                <p class="text-xs text-orange-700 dark:text-emerald-300 mt-1">
                  {{ uniqueMessages }} unique message{{ uniqueMessages !== 1 ? 's' : '' }} • {{ uniqueQueues }} queue{{ uniqueQueues !== 1 ? 's' : '' }}
                </p>
              </div>
            </div>
          </div>

          <!-- Traces Table -->
          <div class="chart-card">
            <div class="chart-body">
              <div class="table-container scrollbar-thin">
                <table class="table">
                <thead>
                  <tr>
                    <th>Event</th>
                    <th>Time</th>
                    <th>Queue</th>
                    <th>Partition</th>
                    <th>Transaction ID</th>
                    <th>Trace Names</th>
                    <th>Data</th>
                    <th>Worker / Group</th>
                    <th class="w-12"></th>
                  </tr>
                </thead>
                <tbody>
                  <tr 
                    v-for="trace in traces" 
                    :key="trace.id"
                    class="border-b border-gray-100 dark:border-gray-800 hover:bg-gray-50 dark:hover:bg-gray-800/50 cursor-pointer transition-colors"
                    @click="viewMessage(trace)"
                  >
                    <!-- Event Type -->
                    <td>
                      <div class="flex items-center gap-2">
                        <div 
                          class="w-2.5 h-2.5 rounded-full flex-shrink-0"
                          :class="getEventColor(trace.event_type)"
                        ></div>
                        <span class="text-xs font-semibold uppercase tracking-wide text-gray-700 dark:text-gray-300">
                          {{ trace.event_type }}
                        </span>
                      </div>
                    </td>
                    
                    <!-- Time -->
                    <td>
                      <span class="text-xs text-gray-600 dark:text-gray-400 whitespace-nowrap">
                        {{ formatDateTime(trace.created_at) }}
                      </span>
                    </td>
                    
                    <!-- Queue -->
                    <td>
                      <span class="text-xs font-medium text-gray-700 dark:text-gray-300">
                        {{ trace.queue_name || 'Unknown' }}
                      </span>
                    </td>
                    
                    <!-- Partition -->
                    <td>
                      <span class="text-xs text-gray-600 dark:text-gray-400">
                        {{ trace.partition_name || 'Unknown' }}
                      </span>
                    </td>
                    
                    <!-- Transaction ID -->
                    <td>
                      <span class="text-xs text-gray-600 dark:text-gray-400">
                        {{ trace.transaction_id }}
                      </span>
                    </td>
                    
                    <!-- Trace Names -->
                    <td>
                      <div v-if="trace.trace_names && trace.trace_names.length > 0" class="flex flex-wrap gap-1">
                        <span 
                          v-for="name in trace.trace_names"
                          :key="name"
                          class="inline-block px-2 py-0.5 text-xs rounded-full whitespace-nowrap"
                          :class="name === currentTraceName 
                            ? 'bg-rose-100 dark:bg-rose-900 text-rose-700 dark:text-rose-300 font-medium'
                            : 'bg-orange-100 dark:bg-orange-900 text-orange-700 dark:text-emerald-300'"
                        >
                          {{ name }}
                        </span>
                      </div>
                      <span v-else class="text-xs text-gray-400">-</span>
                    </td>
                    
                    <!-- Data -->
                    <td class="max-w-xs">
                      <div class="text-xs text-gray-700 dark:text-gray-300">
                        <div v-if="trace.data && trace.data.text" class="truncate" :title="trace.data.text">
                          {{ trace.data.text }}
                        </div>
                        <div v-else-if="hasAdditionalData(trace.data)" class="text-gray-500 dark:text-gray-400 italic">
                          JSON data
                        </div>
                        <span v-else class="text-gray-400">-</span>
                      </div>
                    </td>
                    
                    <!-- Worker / Group -->
                    <td>
                      <div class="text-xs text-gray-600 dark:text-gray-400 space-y-0.5">
                        <div v-if="trace.worker_id" class="truncate" :title="trace.worker_id">
                          {{ trace.worker_id }}
                        </div>
                        <div v-if="trace.consumer_group && trace.consumer_group !== '__QUEUE_MODE__'" class="text-gray-500 dark:text-gray-500">
                          {{ trace.consumer_group }}
                        </div>
                        <span v-if="!trace.worker_id && (!trace.consumer_group || trace.consumer_group === '__QUEUE_MODE__')" class="text-gray-400">-</span>
                      </div>
                    </td>
                    
                    <!-- Action -->
                    <td class="text-right">
                      <svg class="w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
                      </svg>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            
            <!-- Pagination -->
            <div v-if="totalTraces > limit" class="flex items-center justify-between mt-4 pt-4 px-4 border-t border-gray-200 dark:border-gray-700">
              <p class="text-sm text-gray-600 dark:text-gray-400">
                Showing {{ offset + 1 }}-{{ Math.min(offset + limit, totalTraces) }} of {{ totalTraces }}
              </p>
              <div class="flex gap-2">
                <button
                  @click="previousPage"
                  :disabled="offset === 0"
                  class="btn btn-secondary text-sm px-3 py-1.5"
                >
                  Previous
                </button>
                <button
                  @click="nextPage"
                  :disabled="offset + limit >= totalTraces"
                  class="btn btn-secondary text-sm px-3 py-1.5"
                >
                  Next
                </button>
              </div>
            </div>
            </div>
          </div>
        </div>

        <!-- No Results -->
        <div v-else-if="currentTraceName && traces.length === 0" class="chart-card">
          <div class="p-12 text-center text-gray-500 dark:text-gray-400">
            <svg class="w-16 h-16 mx-auto mb-4 text-gray-400 dark:text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.172 16.172a4 4 0 015.656 0M9 10h.01M15 10h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <p class="text-lg font-medium mb-2">No traces found</p>
            <p class="text-sm">No traces found for trace name: <span>{{ currentTraceName }}</span></p>
          </div>
        </div>

        <!-- Available Trace Names -->
        <div v-else class="space-y-4">
          <div class="chart-card">
            <div class="chart-body">
              <h3 class="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-4 px-4 pt-4">Available Trace Names</h3>
              
              <LoadingSpinner v-if="loadingNames" />
              
              <div v-else-if="errorNames" class="error-card">
                <p><strong>Error loading trace names:</strong> {{ errorNames }}</p>
              </div>
              
              <div v-else-if="availableTraceNames.length === 0" class="p-8 text-center text-gray-500 dark:text-gray-400">
                <svg class="w-12 h-12 mx-auto mb-3 text-gray-400 dark:text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                </svg>
                <p class="text-sm">No traces found yet</p>
                <p class="text-xs mt-1">Traces will appear here once messages are processed with trace names</p>
              </div>
              
              <div v-else class="table-container scrollbar-thin">
                <table class="table">
                  <thead>
                    <tr>
                      <th>Trace Name</th>
                      <th>Traces</th>
                      <th>Messages</th>
                      <th>Last Seen</th>
                      <th class="w-12"></th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr 
                      v-for="traceName in availableTraceNames" 
                      :key="traceName.trace_name"
                      @click="selectTraceName(traceName.trace_name)"
                      class="border-b border-gray-100 dark:border-gray-800 hover:bg-gray-50 dark:hover:bg-gray-800/50 cursor-pointer transition-colors"
                    >
                      <!-- Trace Name -->
                      <td>
                        <span class="text-sm font-medium text-gray-900 dark:text-gray-100">
                          {{ traceName.trace_name }}
                        </span>
                      </td>
                      
                      <!-- Trace Count -->
                      <td>
                        <span class="text-sm text-gray-700 dark:text-gray-300">
                          {{ traceName.trace_count }}
                        </span>
                      </td>
                      
                      <!-- Message Count -->
                      <td>
                        <span class="text-sm text-gray-700 dark:text-gray-300">
                          {{ traceName.message_count }}
                        </span>
                      </td>
                      
                      <!-- Last Seen -->
                      <td>
                        <span class="text-sm text-gray-600 dark:text-gray-400 whitespace-nowrap">
                          {{ formatDateTime(traceName.last_seen) }}
                        </span>
                      </td>
                      
                      <!-- Action -->
                      <td class="text-right">
                        <svg class="w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
                        </svg>
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>
              
              <!-- Pagination for trace names -->
              <div v-if="totalTraceNames > limitNames" class="flex items-center justify-between mt-4 pt-4 px-4 border-t border-gray-200 dark:border-gray-700">
                <p class="text-sm text-gray-600 dark:text-gray-400">
                  Showing {{ offsetNames + 1 }}-{{ Math.min(offsetNames + limitNames, totalTraceNames) }} of {{ totalTraceNames }}
                </p>
                <div class="flex gap-2">
                  <button
                    @click="previousNamesPage"
                    :disabled="offsetNames === 0"
                    class="btn btn-secondary text-sm px-3 py-1.5"
                  >
                    Previous
                  </button>
                  <button
                    @click="nextNamesPage"
                    :disabled="offsetNames + limitNames >= totalTraceNames"
                    class="btn btn-secondary text-sm px-3 py-1.5"
                  >
                    Next
                  </button>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Message Detail Panel -->
    <MessageDetailPanel
      :is-open="!!selectedMessage"
      :message="selectedMessage"
      @close="selectedMessage = null"
      @action-complete="onMessageAction"
    />
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { messagesApi } from '../api/messages';
import { useAutoRefresh } from '../composables/useAutoRefresh';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import MessageDetailPanel from '../components/messages/MessageDetailPanel.vue';

const searchTraceName = ref('');
const currentTraceName = ref('');
const traces = ref([]);
const loading = ref(false);
const error = ref(null);
const selectedMessage = ref(null);
const offset = ref(0);
const limit = ref(50);
const totalTraces = ref(0);

// Available trace names state
const availableTraceNames = ref([]);
const loadingNames = ref(false);
const errorNames = ref(null);
const offsetNames = ref(0);
const limitNames = ref(20);
const totalTraceNames = ref(0);

const exampleTraceNames = [
  'order-flow-123',
  'tenant-acme',
  'user-workflow',
];

// Computed stats
const uniqueMessages = computed(() => {
  const messageIds = new Set(traces.value.map(t => t.transaction_id));
  return messageIds.size;
});

const uniqueQueues = computed(() => {
  const queues = new Set(traces.value.map(t => t.queue_name).filter(Boolean));
  return queues.size;
});

async function searchTraces() {
  if (!searchTraceName.value.trim()) return;
  
  loading.value = true;
  error.value = null;
  offset.value = 0;
  currentTraceName.value = searchTraceName.value.trim();
  
  try {
    const response = await messagesApi.getTracesByName(currentTraceName.value, {
      limit: limit.value,
      offset: offset.value
    });
    
    traces.value = response.data.traces || [];
    totalTraces.value = response.data.total || 0;
  } catch (err) {
    error.value = err.response?.data?.error || err.message;
    traces.value = [];
    totalTraces.value = 0;
  } finally {
    loading.value = false;
  }
}

async function loadPage() {
  if (!currentTraceName.value) return;
  
  loading.value = true;
  error.value = null;
  
  try {
    const response = await messagesApi.getTracesByName(currentTraceName.value, {
      limit: limit.value,
      offset: offset.value
    });
    
    traces.value = response.data.traces || [];
    totalTraces.value = response.data.total || 0;
  } catch (err) {
    error.value = err.response?.data?.error || err.message;
  } finally {
    loading.value = false;
  }
}

function previousPage() {
  if (offset.value > 0) {
    offset.value = Math.max(0, offset.value - limit.value);
    loadPage();
  }
}

function nextPage() {
  if (offset.value + limit.value < totalTraces.value) {
    offset.value += limit.value;
    loadPage();
  }
}

function viewMessage(trace) {
  selectedMessage.value = {
    transactionId: trace.transaction_id,
    partitionId: trace.partition_id,
    queue: trace.queue_name,
    partition: trace.partition_name
  };
}

function onMessageAction() {
  selectedMessage.value = null;
  // Optionally refresh traces
}

async function loadAvailableTraceNames() {
  loadingNames.value = true;
  errorNames.value = null;
  
  try {
    const response = await messagesApi.getAvailableTraceNames({
      limit: limitNames.value,
      offset: offsetNames.value
    });
    
    availableTraceNames.value = response.data.trace_names || [];
    totalTraceNames.value = response.data.total || 0;
  } catch (err) {
    errorNames.value = err.response?.data?.error || err.message;
    availableTraceNames.value = [];
    totalTraceNames.value = 0;
  } finally {
    loadingNames.value = false;
  }
}

function selectTraceName(name) {
  searchTraceName.value = name;
  searchTraces();
}

function previousNamesPage() {
  if (offsetNames.value > 0) {
    offsetNames.value = Math.max(0, offsetNames.value - limitNames.value);
    loadAvailableTraceNames();
  }
}

function nextNamesPage() {
  if (offsetNames.value + limitNames.value < totalTraceNames.value) {
    offsetNames.value += limitNames.value;
    loadAvailableTraceNames();
  }
}

function getEventColor(eventType) {
  const colors = {
    info: 'bg-orange-500',
    processing: 'bg-green-500',
    step: 'bg-purple-500',
    error: 'bg-red-500',
    warning: 'bg-yellow-500',
  };
  return colors[eventType] || 'bg-gray-500';
}

function hasAdditionalData(data) {
  if (!data || typeof data !== 'object') return false;
  const keys = Object.keys(data).filter(k => k !== 'text');
  return keys.length > 0;
}

function formatTraceData(data) {
  if (!data || typeof data !== 'object') return '';
  const { text, ...rest } = data;
  return JSON.stringify(rest, null, 2);
}

function formatDateTime(timestamp) {
  if (!timestamp) return '';
  const date = new Date(timestamp);
  return date.toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  });
}

// Auto-refresh function that refreshes appropriate data based on current view
const refreshCurrentView = async () => {
  if (currentTraceName.value) {
    await loadPage();
  } else {
    await loadAvailableTraceNames();
  }
};

// Set up auto-refresh (every 30 seconds)
const autoRefresh = useAutoRefresh(refreshCurrentView, {
  interval: 30000, // 30 seconds
  immediate: false, // Don't auto-load on mount, we do it manually below
  enabled: true,
});

onMounted(() => {
  // Load available trace names on mount
  loadAvailableTraceNames();
  
  // Register refresh callback
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/traces', refreshCurrentView);
  }
});
</script>

<style scoped>
/* Auto-refresh Banner */
.auto-refresh-banner {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-lg px-4 py-3 mb-4;
  @apply flex items-center justify-between;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
}

.dark .auto-refresh-banner {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}

.auto-refresh-toggle {
  @apply flex items-center gap-2 px-3 py-1.5 rounded-lg;
  @apply border border-gray-300 dark:border-gray-600;
  @apply bg-white dark:bg-gray-800;
  @apply text-gray-700 dark:text-gray-300;
  @apply hover:bg-gray-50 dark:hover:bg-gray-700;
  @apply transition-all duration-200;
}

.auto-refresh-toggle.active {
  @apply bg-green-50 dark:bg-green-900/20;
  @apply border-green-500 dark:border-green-600;
  @apply text-green-700 dark:text-green-400;
  @apply hover:bg-green-100 dark:hover:bg-green-900/30;
}

.manual-refresh-button {
  @apply flex items-center gap-2 px-3 py-1.5 rounded-lg;
  @apply bg-rose-600 dark:bg-rose-600;
  @apply text-white;
  @apply hover:bg-rose-700 dark:hover:bg-rose-700;
  @apply disabled:opacity-50 disabled:cursor-not-allowed;
  @apply transition-all duration-200;
  @apply font-medium;
}

.manual-refresh-button:disabled {
  @apply hover:bg-rose-600;
}
</style>
