<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
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
          <div class="bg-blue-50 dark:bg-blue-900/20 border border-blue-200 dark:border-blue-700/30 rounded-lg p-4">
            <div class="flex items-center gap-3">
              <svg class="w-5 h-5 text-blue-600 dark:text-blue-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <div class="flex-1">
                <p class="text-sm font-semibold text-blue-900 dark:text-blue-100">
                  Found {{ totalTraces }} trace{{ totalTraces !== 1 ? 's' : '' }} for: <span class="font-mono">{{ currentTraceName }}</span>
                </p>
                <p class="text-xs text-blue-700 dark:text-blue-300 mt-1">
                  {{ uniqueMessages }} unique message{{ uniqueMessages !== 1 ? 's' : '' }} • {{ uniqueQueues }} queue{{ uniqueQueues !== 1 ? 's' : '' }}
                </p>
              </div>
            </div>
          </div>

          <!-- Traces Timeline -->
          <div class="data-card">
            <div class="p-4">
              <h3 class="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-4">Processing Timeline</h3>
              
              <div class="space-y-3">
                <div 
                  v-for="trace in traces" 
                  :key="trace.id"
                  class="border border-gray-200 dark:border-gray-700 rounded-lg p-4 hover:border-rose-300 dark:hover:border-rose-600 transition-colors cursor-pointer"
                  @click="viewMessage(trace)"
                >
                  <!-- Trace Header -->
                  <div class="flex items-start justify-between mb-3">
                    <div class="flex items-center gap-3">
                      <div 
                        class="w-3 h-3 rounded-full flex-shrink-0 mt-1"
                        :class="getEventColor(trace.event_type)"
                      ></div>
                      <div>
                        <div class="flex items-center gap-2">
                          <span class="text-xs font-semibold uppercase tracking-wide text-gray-700 dark:text-gray-300">
                            {{ trace.event_type }}
                          </span>
                          <span class="text-xs text-gray-500 dark:text-gray-400">
                            {{ formatDateTime(trace.created_at) }}
                          </span>
                        </div>
                        
                        <!-- Message Info -->
                        <div class="flex items-center gap-3 mt-1 text-xs text-gray-600 dark:text-gray-400">
                          <span class="font-medium">{{ trace.queue_name || 'Unknown Queue' }}</span>
                          <span>•</span>
                          <span class="font-mono">{{ trace.partition_name || 'Unknown' }}</span>
                          <span>•</span>
                          <span class="font-mono text-[10px]">{{ trace.transaction_id }}</span>
                        </div>
                      </div>
                    </div>
                    
                    <!-- Click indicator -->
                    <svg class="w-5 h-5 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
                    </svg>
                  </div>

                  <!-- Trace Names -->
                  <div v-if="trace.trace_names && trace.trace_names.length > 0" class="mb-3">
                    <div class="flex flex-wrap gap-1">
                      <span 
                        v-for="name in trace.trace_names"
                        :key="name"
                        class="inline-block px-2 py-0.5 text-xs rounded-full"
                        :class="name === currentTraceName 
                          ? 'bg-rose-100 dark:bg-rose-900 text-rose-700 dark:text-rose-300 font-medium'
                          : 'bg-blue-100 dark:bg-blue-900 text-blue-700 dark:text-blue-300'"
                      >
                        {{ name }}
                      </span>
                    </div>
                  </div>

                  <!-- Trace Data -->
                  <div class="text-sm text-gray-700 dark:text-gray-300">
                    <div v-if="trace.data && trace.data.text" class="mb-2">
                      {{ trace.data.text }}
                    </div>
                    
                    <div v-if="hasAdditionalData(trace.data)" class="bg-gray-50 dark:bg-slate-900 rounded p-2 text-xs font-mono">
                      <pre class="overflow-x-auto scrollbar-thin">{{ formatTraceData(trace.data) }}</pre>
                    </div>
                  </div>

                  <!-- Metadata Footer -->
                  <div class="flex gap-4 mt-3 pt-3 border-t border-gray-200 dark:border-gray-700 text-xs text-gray-500 dark:text-gray-400">
                    <span v-if="trace.worker_id">
                      <strong>Worker:</strong> {{ trace.worker_id }}
                    </span>
                    <span v-if="trace.consumer_group && trace.consumer_group !== '__QUEUE_MODE__'">
                      <strong>Group:</strong> {{ trace.consumer_group }}
                    </span>
                  </div>
                </div>
              </div>

              <!-- Pagination -->
              <div v-if="totalTraces > limit" class="flex items-center justify-between mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
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
        <div v-else-if="currentTraceName && traces.length === 0" class="table-flat">
          <div class="p-12 text-center text-gray-500 dark:text-gray-400">
            <svg class="w-16 h-16 mx-auto mb-4 text-gray-400 dark:text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.172 16.172a4 4 0 015.656 0M9 10h.01M15 10h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <p class="text-lg font-medium mb-2">No traces found</p>
            <p class="text-sm">No traces found for trace name: <span class="font-mono">{{ currentTraceName }}</span></p>
          </div>
        </div>

        <!-- Available Trace Names -->
        <div v-else class="space-y-4">
          <div class="data-card">
            <div class="p-4">
              <h3 class="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-4">Available Trace Names</h3>
              
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
              
              <div v-else class="space-y-2">
                <div 
                  v-for="traceName in availableTraceNames" 
                  :key="traceName.trace_name"
                  @click="selectTraceName(traceName.trace_name)"
                  class="border border-gray-200 dark:border-gray-700 rounded-lg p-4 hover:border-rose-300 dark:hover:border-rose-600 hover:bg-gray-50 dark:hover:bg-gray-800/50 transition-colors cursor-pointer"
                >
                  <div class="flex items-start justify-between">
                    <div class="flex-1 min-w-0">
                      <div class="flex items-center gap-3">
                        <span class="text-base font-medium text-gray-900 dark:text-gray-100 font-mono">
                          {{ traceName.trace_name }}
                        </span>
                      </div>
                      <div class="flex items-center gap-4 mt-2 text-xs text-gray-600 dark:text-gray-400">
                        <span>
                          <strong>{{ traceName.trace_count }}</strong> trace{{ traceName.trace_count !== 1 ? 's' : '' }}
                        </span>
                        <span>•</span>
                        <span>
                          <strong>{{ traceName.message_count }}</strong> message{{ traceName.message_count !== 1 ? 's' : '' }}
                        </span>
                        <span>•</span>
                        <span>
                          Last: {{ formatDateTime(traceName.last_seen) }}
                        </span>
                      </div>
                    </div>
                    <svg class="w-5 h-5 text-gray-400 flex-shrink-0 ml-2" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
                    </svg>
                  </div>
                </div>
              </div>
              
              <!-- Pagination for trace names -->
              <div v-if="totalTraceNames > limitNames" class="flex items-center justify-between mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
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
    info: 'bg-blue-500',
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

onMounted(() => {
  // Load available trace names on mount
  loadAvailableTraceNames();
  
  // Register refresh callback
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/traces', () => {
      if (currentTraceName.value) {
        loadPage();
      } else {
        loadAvailableTraceNames();
      }
    });
  }
});
</script>

