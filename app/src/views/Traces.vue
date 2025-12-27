<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Search Box -->
    <div class="card">
      <div class="card-body">
        <div class="flex flex-col sm:flex-row gap-3">
          <div class="flex-1 relative">
            <input
              v-model="searchTraceName"
              @keyup.enter="searchTraces"
              type="text"
              placeholder="Enter trace name (e.g., tenant-acme, order-flow-123)"
              class="input w-full pl-10"
            />
            <svg class="w-5 h-5 text-light-400 absolute left-3 top-1/2 -translate-y-1/2" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
            </svg>
          </div>
          <button
            @click="searchTraces"
            :disabled="!searchTraceName || loading"
            class="btn btn-primary px-6"
          >
            Search
          </button>
          <button
            v-if="currentTraceName"
            @click="clearSearch"
            class="btn btn-secondary"
          >
            Clear
          </button>
        </div>
        
        <!-- Quick Examples -->
        <div v-if="!currentTraceName && exampleTraceNames.length > 0" class="flex flex-wrap items-center gap-2 mt-3 text-xs">
          <span class="text-light-500">Try:</span>
          <button
            v-for="example in exampleTraceNames"
            :key="example"
            @click="searchTraceName = example; searchTraces()"
            class="px-2 py-1 bg-light-100 dark:bg-dark-200 hover:bg-light-200 dark:hover:bg-dark-100 rounded text-light-700 dark:text-light-300 transition-colors"
          >
            {{ example }}
          </button>
        </div>
      </div>
    </div>

    <!-- Results Summary -->
    <div v-if="currentTraceName && traces.length > 0" class="card bg-amber-50 dark:bg-amber-900/20 border-amber-200 dark:border-amber-700/30">
      <div class="card-body py-3">
        <div class="flex items-center gap-3">
          <svg class="w-5 h-5 text-amber-600 dark:text-amber-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          <div>
            <p class="text-sm font-semibold text-amber-900 dark:text-amber-100">
              Found {{ totalTraces }} trace{{ totalTraces !== 1 ? 's' : '' }} for: <span class="text-amber-700 dark:text-amber-300">{{ currentTraceName }}</span>
            </p>
            <p class="text-xs text-amber-700 dark:text-amber-300 mt-0.5">
              {{ uniqueMessages }} unique message{{ uniqueMessages !== 1 ? 's' : '' }} • {{ uniqueQueues }} queue{{ uniqueQueues !== 1 ? 's' : '' }}
            </p>
          </div>
        </div>
      </div>
    </div>

    <!-- Loading State -->
    <div v-if="loading" class="card">
      <div class="card-body py-12 text-center">
        <div class="spinner mx-auto mb-3" />
        <p class="text-sm text-light-500">Loading traces...</p>
      </div>
    </div>

    <!-- Error State -->
    <div v-else-if="error" class="card bg-rose-50 dark:bg-rose-900/20 border-rose-200 dark:border-rose-700/30">
      <div class="card-body">
        <p class="text-sm text-rose-700 dark:text-rose-300">
          <strong>Error:</strong> {{ error }}
        </p>
      </div>
    </div>

    <!-- Traces Table -->
    <div v-else-if="currentTraceName && traces.length > 0" class="card">
      <div class="overflow-x-auto">
        <table class="w-full text-sm">
          <thead>
            <tr class="border-b border-light-200 dark:border-dark-100">
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Event</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Time</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Queue</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Partition</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Transaction ID</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Trace Names</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Data</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Worker / Group</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-light-100 dark:divide-dark-100">
            <tr 
              v-for="trace in traces" 
              :key="trace.id"
              class="hover:bg-light-50 dark:hover:bg-dark-200 cursor-pointer transition-colors"
              @click="viewTrace(trace)"
            >
              <!-- Event Type -->
              <td class="px-4 py-3">
                <div class="flex items-center gap-2">
                  <span 
                    class="w-2 h-2 rounded-full flex-shrink-0"
                    :class="getEventColor(trace.event_type)"
                  />
                  <span class="text-xs font-semibold uppercase tracking-wide text-light-700 dark:text-light-300">
                    {{ trace.event_type }}
                  </span>
                </div>
              </td>
              
              <!-- Time -->
              <td class="px-4 py-3">
                <span class="text-xs text-light-600 dark:text-light-400 whitespace-nowrap">
                  {{ formatDateTime(trace.created_at) }}
                </span>
              </td>
              
              <!-- Queue -->
              <td class="px-4 py-3">
                <span class="text-xs font-medium text-light-700 dark:text-light-300">
                  {{ trace.queue_name || '-' }}
                </span>
              </td>
              
              <!-- Partition -->
              <td class="px-4 py-3">
                <span class="text-xs text-light-600 dark:text-light-400">
                  {{ trace.partition_name || '-' }}
                </span>
              </td>
              
              <!-- Transaction ID -->
              <td class="px-4 py-3">
                <span class="text-xs text-light-600 dark:text-light-400 font-mono">
                  {{ trace.transaction_id?.slice(0, 8) }}...
                </span>
              </td>
              
              <!-- Trace Names -->
              <td class="px-4 py-3">
                <div v-if="trace.trace_names?.length > 0" class="flex flex-wrap gap-1">
                  <span 
                    v-for="name in trace.trace_names"
                    :key="name"
                    class="badge text-xs"
                    :class="name === currentTraceName ? 'badge-queen' : 'badge-secondary'"
                  >
                    {{ name }}
                  </span>
                </div>
                <span v-else class="text-xs text-light-400">-</span>
              </td>
              
              <!-- Data -->
              <td class="px-4 py-3 max-w-xs">
                <div class="text-xs text-light-700 dark:text-light-300 truncate">
                  <span v-if="trace.data?.text" :title="trace.data.text">{{ trace.data.text }}</span>
                  <span v-else-if="hasAdditionalData(trace.data)" class="text-light-500 italic">JSON data</span>
                  <span v-else class="text-light-400">-</span>
                </div>
              </td>
              
              <!-- Worker / Group -->
              <td class="px-4 py-3">
                <div class="text-xs text-light-600 dark:text-light-400 space-y-0.5">
                  <div v-if="trace.worker_id" class="truncate max-w-[120px]" :title="trace.worker_id">
                    {{ trace.worker_id }}
                  </div>
                  <div v-if="trace.consumer_group && trace.consumer_group !== '__QUEUE_MODE__'" class="text-light-500">
                    {{ trace.consumer_group }}
                  </div>
                  <span v-if="!trace.worker_id && (!trace.consumer_group || trace.consumer_group === '__QUEUE_MODE__')" class="text-light-400">-</span>
                </div>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      
      <!-- Pagination -->
      <div v-if="totalTraces > limit" class="px-4 py-3 border-t border-light-200 dark:border-dark-100 flex items-center justify-between">
        <p class="text-sm text-light-600 dark:text-light-400">
          Showing {{ offset + 1 }}-{{ Math.min(offset + limit, totalTraces) }} of {{ totalTraces }}
        </p>
        <div class="flex gap-2">
          <button
            @click="previousPage"
            :disabled="offset === 0"
            class="btn btn-secondary text-sm"
          >
            Previous
          </button>
          <button
            @click="nextPage"
            :disabled="offset + limit >= totalTraces"
            class="btn btn-secondary text-sm"
          >
            Next
          </button>
        </div>
      </div>
    </div>

    <!-- No Results -->
    <div v-else-if="currentTraceName && traces.length === 0 && !loading" class="card">
      <div class="card-body py-12 text-center">
        <svg class="w-16 h-16 mx-auto mb-4 text-light-300 dark:text-light-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M9.172 16.172a4 4 0 015.656 0M9 10h.01M15 10h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
        </svg>
        <p class="text-lg font-medium text-light-700 dark:text-light-300 mb-1">No traces found</p>
        <p class="text-sm text-light-500">No traces found for: <span class="font-medium">{{ currentTraceName }}</span></p>
      </div>
    </div>

    <!-- Available Trace Names (Default View) -->
    <div v-else-if="!currentTraceName" class="card">
      <div class="card-header">
        <h3 class="font-semibold text-light-900 dark:text-white">Available Trace Names</h3>
        <p class="text-xs text-light-500 mt-0.5">Click a trace name to view all related events</p>
      </div>
      
      <div v-if="loadingNames" class="card-body py-12 text-center">
        <div class="spinner mx-auto mb-3" />
        <p class="text-sm text-light-500">Loading trace names...</p>
      </div>
      
      <div v-else-if="errorNames" class="card-body">
        <p class="text-sm text-rose-700 dark:text-rose-300">
          <strong>Error:</strong> {{ errorNames }}
        </p>
      </div>
      
      <div v-else-if="availableTraceNames.length === 0" class="card-body py-12 text-center">
        <svg class="w-12 h-12 mx-auto mb-3 text-light-300 dark:text-light-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
        </svg>
        <p class="text-sm text-light-500">No traces found yet</p>
        <p class="text-xs text-light-400 mt-1">Traces will appear here once messages are processed with trace names</p>
      </div>
      
      <div v-else class="overflow-x-auto">
        <table class="w-full text-sm">
          <thead>
            <tr class="border-b border-light-200 dark:border-dark-100">
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Trace Name</th>
              <th class="px-4 py-3 text-right text-xs font-semibold text-light-500 uppercase tracking-wider">Traces</th>
              <th class="px-4 py-3 text-right text-xs font-semibold text-light-500 uppercase tracking-wider">Messages</th>
              <th class="px-4 py-3 text-left text-xs font-semibold text-light-500 uppercase tracking-wider">Last Seen</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-light-100 dark:divide-dark-100">
            <tr 
              v-for="traceName in availableTraceNames" 
              :key="traceName.trace_name"
              @click="selectTraceName(traceName.trace_name)"
              class="hover:bg-light-50 dark:hover:bg-dark-200 cursor-pointer transition-colors"
            >
              <td class="px-4 py-3">
                <span class="font-medium text-light-900 dark:text-light-100">
                  {{ traceName.trace_name }}
                </span>
              </td>
              <td class="px-4 py-3 text-right">
                <span class="badge badge-queen">{{ traceName.trace_count }}</span>
              </td>
              <td class="px-4 py-3 text-right">
                <span class="badge badge-secondary">{{ traceName.message_count }}</span>
              </td>
              <td class="px-4 py-3">
                <span class="text-xs text-light-600 dark:text-light-400 whitespace-nowrap">
                  {{ formatDateTime(traceName.last_seen) }}
                </span>
              </td>
            </tr>
          </tbody>
        </table>
        
        <!-- Pagination for trace names -->
        <div v-if="totalTraceNames > limitNames" class="px-4 py-3 border-t border-light-200 dark:border-dark-100 flex items-center justify-between">
          <p class="text-sm text-light-600 dark:text-light-400">
            Showing {{ offsetNames + 1 }}-{{ Math.min(offsetNames + limitNames, totalTraceNames) }} of {{ totalTraceNames }}
          </p>
          <div class="flex gap-2">
            <button
              @click="previousNamesPage"
              :disabled="offsetNames === 0"
              class="btn btn-secondary text-sm"
            >
              Previous
            </button>
            <button
              @click="nextNamesPage"
              :disabled="offsetNames + limitNames >= totalTraceNames"
              class="btn btn-secondary text-sm"
            >
              Next
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Trace Detail Modal -->
    <div 
      v-if="selectedTrace"
      class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-dark-500/50 backdrop-blur-sm"
      @click.self="selectedTrace = null"
    >
      <div class="bg-white dark:bg-dark-300 rounded-xl shadow-2xl w-full max-w-2xl max-h-[80vh] overflow-hidden">
        <div class="px-6 py-4 border-b border-light-200 dark:border-dark-100 flex items-center justify-between">
          <div class="flex items-center gap-3">
            <span 
              class="w-3 h-3 rounded-full"
              :class="getEventColor(selectedTrace.event_type)"
            />
            <h3 class="font-semibold text-light-900 dark:text-white uppercase tracking-wide">
              {{ selectedTrace.event_type }} Trace
            </h3>
          </div>
          <button @click="selectedTrace = null" class="p-1 hover:bg-light-100 dark:hover:bg-dark-200 rounded-lg transition-colors">
            <svg class="w-5 h-5 text-light-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        <div class="p-6 overflow-y-auto max-h-[60vh] space-y-4">
          <!-- Basic Info Grid -->
          <div class="grid grid-cols-2 gap-4 text-sm">
            <div>
              <label class="text-xs text-light-500 uppercase tracking-wider">Time</label>
              <p class="text-light-900 dark:text-light-100">{{ formatDateTime(selectedTrace.created_at) }}</p>
            </div>
            <div>
              <label class="text-xs text-light-500 uppercase tracking-wider">Queue</label>
              <p class="text-light-900 dark:text-light-100">{{ selectedTrace.queue_name || '-' }}</p>
            </div>
            <div>
              <label class="text-xs text-light-500 uppercase tracking-wider">Partition</label>
              <p class="text-light-900 dark:text-light-100">{{ selectedTrace.partition_name || '-' }}</p>
            </div>
            <div>
              <label class="text-xs text-light-500 uppercase tracking-wider">Transaction ID</label>
              <p class="font-mono text-light-900 dark:text-light-100 text-xs break-all">{{ selectedTrace.transaction_id }}</p>
            </div>
            <div v-if="selectedTrace.worker_id">
              <label class="text-xs text-light-500 uppercase tracking-wider">Worker ID</label>
              <p class="font-mono text-light-900 dark:text-light-100 text-xs break-all">{{ selectedTrace.worker_id }}</p>
            </div>
            <div v-if="selectedTrace.consumer_group && selectedTrace.consumer_group !== '__QUEUE_MODE__'">
              <label class="text-xs text-light-500 uppercase tracking-wider">Consumer Group</label>
              <p class="text-light-900 dark:text-light-100">{{ selectedTrace.consumer_group }}</p>
            </div>
          </div>

          <!-- Trace Names -->
          <div v-if="selectedTrace.trace_names?.length > 0">
            <label class="text-xs text-light-500 uppercase tracking-wider block mb-2">Trace Names</label>
            <div class="flex flex-wrap gap-2">
              <span 
                v-for="name in selectedTrace.trace_names"
                :key="name"
                class="badge"
                :class="name === currentTraceName ? 'badge-queen' : 'badge-secondary'"
              >
                {{ name }}
              </span>
            </div>
          </div>

          <!-- Trace Data -->
          <div v-if="selectedTrace.data">
            <label class="text-xs text-light-500 uppercase tracking-wider block mb-2">Trace Data</label>
            
            <!-- Text content -->
            <div v-if="selectedTrace.data.text" class="mb-3">
              <p class="text-sm text-light-900 dark:text-light-100 bg-light-100 dark:bg-dark-200 rounded-lg p-3">
                {{ selectedTrace.data.text }}
              </p>
            </div>
            
            <!-- JSON data (excluding text) -->
            <div v-if="hasAdditionalData(selectedTrace.data)">
              <pre class="text-xs font-mono bg-light-100 dark:bg-dark-200 rounded-lg p-3 overflow-x-auto text-light-800 dark:text-light-200">{{ formatTraceData(selectedTrace.data) }}</pre>
            </div>
          </div>

          <!-- Link to Message -->
          <div class="pt-4 border-t border-light-200 dark:border-dark-100">
            <router-link 
              :to="`/messages?partitionId=${selectedTrace.partition_id}&transactionId=${selectedTrace.transaction_id}`"
              class="btn btn-secondary w-full text-sm"
              @click="selectedTrace = null"
            >
              <svg class="w-4 h-4 mr-2" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 6H6a2 2 0 00-2 2v10a2 2 0 002 2h10a2 2 0 002-2v-4M14 4h6m0 0v6m0-6L10 14" />
              </svg>
              View Full Message
            </router-link>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { traces as tracesApi } from '@/api'
import { useRefresh } from '@/composables/useRefresh'

// Search state
const searchTraceName = ref('')
const currentTraceName = ref('')
const traces = ref([])
const loading = ref(false)
const error = ref(null)
const selectedTrace = ref(null)
const offset = ref(0)
const limit = ref(50)
const totalTraces = ref(0)

// Available trace names state
const availableTraceNames = ref([])
const loadingNames = ref(false)
const errorNames = ref(null)
const offsetNames = ref(0)
const limitNames = ref(20)
const totalTraceNames = ref(0)

const exampleTraceNames = [
  'order-flow-123',
  'tenant-acme',
  'user-workflow',
]

// Computed stats
const uniqueMessages = computed(() => {
  const messageIds = new Set(traces.value.map(t => t.transaction_id))
  return messageIds.size
})

const uniqueQueues = computed(() => {
  const queues = new Set(traces.value.map(t => t.queue_name).filter(Boolean))
  return queues.size
})

// Search traces by name
async function searchTraces() {
  if (!searchTraceName.value.trim()) return
  
  loading.value = true
  error.value = null
  offset.value = 0
  currentTraceName.value = searchTraceName.value.trim()
  
  try {
    const response = await tracesApi.getByName(currentTraceName.value, {
      limit: limit.value,
      offset: offset.value
    })
    
    traces.value = response.data.traces || []
    totalTraces.value = response.data.total || 0
  } catch (err) {
    error.value = err.response?.data?.error || err.message
    traces.value = []
    totalTraces.value = 0
  } finally {
    loading.value = false
  }
}

// Load page of traces
async function loadPage() {
  if (!currentTraceName.value) return
  
  loading.value = true
  error.value = null
  
  try {
    const response = await tracesApi.getByName(currentTraceName.value, {
      limit: limit.value,
      offset: offset.value
    })
    
    traces.value = response.data.traces || []
    totalTraces.value = response.data.total || 0
  } catch (err) {
    error.value = err.response?.data?.error || err.message
  } finally {
    loading.value = false
  }
}

function previousPage() {
  if (offset.value > 0) {
    offset.value = Math.max(0, offset.value - limit.value)
    loadPage()
  }
}

function nextPage() {
  if (offset.value + limit.value < totalTraces.value) {
    offset.value += limit.value
    loadPage()
  }
}

function clearSearch() {
  searchTraceName.value = ''
  currentTraceName.value = ''
  traces.value = []
  totalTraces.value = 0
  offset.value = 0
  loadAvailableTraceNames()
}

function viewTrace(trace) {
  selectedTrace.value = trace
}

// Load available trace names
async function loadAvailableTraceNames() {
  if (!availableTraceNames.value.length) loadingNames.value = true
  errorNames.value = null
  
  try {
    const response = await tracesApi.getAvailableNames({
      limit: limitNames.value,
      offset: offsetNames.value
    })
    
    availableTraceNames.value = response.data.trace_names || []
    totalTraceNames.value = response.data.total || 0
  } catch (err) {
    errorNames.value = err.response?.data?.error || err.message
    availableTraceNames.value = []
    totalTraceNames.value = 0
  } finally {
    loadingNames.value = false
  }
}

function selectTraceName(name) {
  searchTraceName.value = name
  searchTraces()
}

function previousNamesPage() {
  if (offsetNames.value > 0) {
    offsetNames.value = Math.max(0, offsetNames.value - limitNames.value)
    loadAvailableTraceNames()
  }
}

function nextNamesPage() {
  if (offsetNames.value + limitNames.value < totalTraceNames.value) {
    offsetNames.value += limitNames.value
    loadAvailableTraceNames()
  }
}

// Event type colors
function getEventColor(eventType) {
  const colors = {
    info: 'bg-blue-500',
    processing: 'bg-emerald-500',
    step: 'bg-violet-500',
    error: 'bg-rose-500',
    warning: 'bg-amber-500',
  }
  return colors[eventType] || 'bg-light-400'
}

function hasAdditionalData(data) {
  if (!data || typeof data !== 'object') return false
  const keys = Object.keys(data).filter(k => k !== 'text')
  return keys.length > 0
}

function formatTraceData(data) {
  if (!data || typeof data !== 'object') return ''
  const { text, ...rest } = data
  return JSON.stringify(rest, null, 2)
}

function formatDateTime(timestamp) {
  if (!timestamp) return ''
  const date = new Date(timestamp)
  return date.toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  })
}

// Refresh function
const refreshCurrentView = async () => {
  if (currentTraceName.value) {
    await loadPage()
  } else {
    await loadAvailableTraceNames()
  }
}

// Register for global refresh
useRefresh(refreshCurrentView)

onMounted(() => {
  loadAvailableTraceNames()
})
</script>

