<template>
  <div class="space-y-4 sm:space-y-6 animate-fade-in">
    <!-- Mode Indicator (for Bus Mode) -->
    <div v-if="queueMode && queueMode.type === 'bus'" class="bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-700/30 rounded-lg p-3">
      <div class="flex items-center gap-2 text-sm">
        <svg class="w-5 h-5 text-purple-600 dark:text-purple-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
        </svg>
        <span class="font-medium text-purple-900 dark:text-purple-100">Bus Mode Active</span>
        <span class="text-purple-700 dark:text-purple-300">{{ queueMode.busGroupsCount }} consumer group(s)</span>
      </div>
    </div>

    <!-- Filters -->
    <div class="card p-3 sm:p-4">
      <div class="flex flex-col gap-3 sm:gap-4">
        <!-- First Row: Search, Queue, Status, Limit -->
        <div class="grid grid-cols-2 sm:flex sm:flex-wrap sm:items-end gap-2 sm:gap-4">
          <div class="col-span-2 sm:flex-1 sm:min-w-[200px] sm:max-w-sm">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">
              Search
            </label>
            <div class="relative">
              <input
                v-model="searchQuery"
                type="text"
                placeholder="Search by transaction ID..."
                class="input pl-10"
              />
              <svg 
                class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-light-500"
                fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
              >
                <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
              </svg>
            </div>
          </div>
          
          <div class="col-span-2 sm:w-48">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">
              Queue
            </label>
            <select v-model="filterQueue" class="select">
              <option value="">All Queues</option>
              <option v-for="q in queues" :key="q.name" :value="q.name">
                {{ q.name }}
              </option>
            </select>
          </div>
          
          <div class="sm:w-40">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">
              Status
            </label>
            <select v-model="filterStatus" class="select">
              <option value="">All Status</option>
              <option value="pending">Pending</option>
              <option value="processing">Processing</option>
              <option value="completed">Completed</option>
              <option value="dead_letter">Dead Letter</option>
            </select>
          </div>
          
          <div class="sm:w-40">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">
              Limit
            </label>
            <select v-model="limit" class="select">
              <option :value="50">50 messages</option>
              <option :value="100">100 messages</option>
              <option :value="200">200 messages</option>
              <option :value="500">500 messages</option>
            </select>
          </div>
        </div>
        
        <!-- Second Row: Date Range and Actions -->
        <div class="grid grid-cols-2 sm:flex sm:flex-wrap sm:items-end gap-2 sm:gap-4">
          <div class="col-span-1 sm:flex-1 sm:min-w-[180px] sm:max-w-xs">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">
              From
            </label>
            <input
              v-model="filterFrom"
              type="datetime-local"
              class="input text-sm"
            />
          </div>
          
          <div class="col-span-1 sm:flex-1 sm:min-w-[180px] sm:max-w-xs">
            <label class="block text-xs font-medium text-light-600 dark:text-light-400 mb-1.5">
              To
            </label>
            <input
              v-model="filterTo"
              type="datetime-local"
              class="input text-sm"
            />
          </div>
          
          <!-- Quick Time Range Buttons -->
          <div class="col-span-2 flex flex-wrap gap-2">
            <button @click="setTimeRange(1)" class="btn btn-secondary text-xs sm:text-sm flex-1 sm:flex-none">1h</button>
            <button @click="setTimeRange(24)" class="btn btn-secondary text-xs sm:text-sm flex-1 sm:flex-none">24h</button>
            <button @click="setTimeRange(168)" class="btn btn-secondary text-xs sm:text-sm flex-1 sm:flex-none">7d</button>
            <button @click="applyFilters" class="btn btn-cyber text-xs sm:text-sm flex-1 sm:flex-none">
              Apply
            </button>
            <button 
              v-if="hasActiveFilters" 
              @click="clearFilters" 
              class="btn btn-secondary text-xs sm:text-sm"
            >
              Clear
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Messages list -->
    <div class="card">
      <div class="card-header flex items-center justify-between">
        <div class="flex items-center gap-3">
          <h3 class="font-semibold text-light-900 dark:text-white">Messages</h3>
          <span class="badge badge-queen">{{ formatNumber(messages.length) }} loaded</span>
        </div>
        <div class="text-sm text-light-500">
          Page {{ currentPage }}
        </div>
      </div>
      
      <div class="overflow-x-auto">
        <table class="table">
          <thead>
            <tr>
              <th>Queue</th>
              <th class="hidden xl:table-cell">Partition ID</th>
              <th class="hidden lg:table-cell">Partition</th>
              <th>Transaction ID</th>
              <th class="text-right">Created</th>
              <th class="text-right">Status</th>
            </tr>
          </thead>
          <tbody>
            <template v-if="loading">
              <tr v-for="i in 10" :key="i">
                <td><div class="skeleton h-4 w-24" /></td>
                <td class="hidden xl:table-cell"><div class="skeleton h-4 w-32" /></td>
                <td class="hidden lg:table-cell"><div class="skeleton h-4 w-12" /></td>
                <td><div class="skeleton h-4 w-40" /></td>
                <td><div class="skeleton h-4 w-28" /></td>
                <td><div class="skeleton h-4 w-20" /></td>
              </tr>
            </template>
            <template v-else-if="filteredMessages.length > 0">
              <tr 
                v-for="message in filteredMessages" 
                :key="message.id"
                class="group cursor-pointer hover:bg-queen-50 dark:hover:bg-queen-500/10"
                @click="selectMessage(message)"
              >
                <td>
                  <div class="text-sm font-medium text-light-900 dark:text-light-100">{{ message.queue }}</div>
                  <div class="text-xs text-light-500 dark:text-light-400 lg:hidden mt-0.5">
                    {{ message.partition }}
                  </div>
                </td>
                <td class="hidden xl:table-cell">
                  <div class="text-xs text-light-600 dark:text-light-400 font-mono select-all break-all">
                    {{ message.partitionId }}
                  </div>
                </td>
                <td class="hidden lg:table-cell">
                  <span class="text-xs text-light-600 dark:text-light-400">{{ message.partition }}</span>
                </td>
                <td>
                  <div class="text-xs font-mono select-all break-all text-light-900 dark:text-light-100">
                    {{ message.transactionId }}
                  </div>
                </td>
                <td class="text-right text-xs text-light-600 dark:text-light-400 tabular-nums whitespace-nowrap">
                  {{ formatDateTime(message.createdAt) }}
                </td>
                <td class="text-right">
                  <div class="flex flex-col items-end gap-1">
                    <span 
                      class="badge"
                      :class="{
                        'badge-cyber': message.status === 'pending',
                        'badge-crown': message.status === 'processing',
                        'badge-success': message.status === 'completed',
                        'badge-danger': message.status === 'dead_letter' || message.status === 'failed'
                      }"
                    >
                      {{ message.status }}
                    </span>
                    <div v-if="message.busStatus && message.busStatus.totalGroups > 0" class="text-xs text-light-600 dark:text-light-400">
                      {{ message.busStatus.consumedBy }}/{{ message.busStatus.totalGroups }} groups
                    </div>
                  </div>
                </td>
              </tr>
            </template>
            <tr v-else>
              <td colspan="6" class="text-center py-12">
                <svg class="w-12 h-12 mx-auto text-light-400 dark:text-light-600 mb-3" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M21.75 6.75v10.5a2.25 2.25 0 01-2.25 2.25h-15a2.25 2.25 0 01-2.25-2.25V6.75m19.5 0A2.25 2.25 0 0019.5 4.5h-15a2.25 2.25 0 00-2.25 2.25m19.5 0v.243a2.25 2.25 0 01-1.07 1.916l-7.5 4.615a2.25 2.25 0 01-2.36 0L3.32 8.91a2.25 2.25 0 01-1.07-1.916V6.75" />
                </svg>
                <p class="text-light-600 dark:text-light-400">No messages found</p>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      
      <!-- Pagination -->
      <div class="px-5 py-4 border-t border-light-200 dark:border-dark-50 flex items-center justify-between">
        <div class="text-sm text-light-600 dark:text-light-400">
          Page {{ currentPage }}
        </div>
        <div class="flex gap-2">
          <button
            @click="prevPage"
            :disabled="currentPage === 1"
            class="btn btn-secondary px-3 py-1.5"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
            </svg>
          </button>
          <button
            @click="nextPage"
            :disabled="messages.length < limit"
            class="btn btn-secondary px-3 py-1.5"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
            </svg>
          </button>
        </div>
      </div>
    </div>

    <!-- Message detail panel (teleported to body to avoid transform issues) -->
    <Teleport to="body">
      <div 
        v-if="selectedMessage"
        class="fixed inset-0 sm:left-auto w-full sm:max-w-2xl bg-light-50 dark:bg-dark-400 shadow-2xl z-50 overflow-y-auto border-l border-light-200 dark:border-dark-50"
      >
      <div class="p-4 sm:p-6">
        <!-- Header -->
        <div class="flex items-start justify-between mb-5 pb-4 border-b border-light-200 dark:border-dark-50">
          <div class="flex-1 min-w-0">
            <h3 class="text-base font-bold mb-1 text-light-900 dark:text-white">Message Details</h3>
            <p class="text-xs font-mono text-light-500 break-all">
              {{ messageDetail?.transactionId }}
            </p>
          </div>
          <button 
            @click="selectedMessage = null"
            class="text-light-400 hover:text-light-600 dark:hover:text-light-300 p-1.5 rounded-lg hover:bg-light-100 dark:hover:bg-dark-300 transition-colors"
          >
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>

        <div v-if="detailLoading" class="text-center py-12">
          <div class="w-8 h-8 border-2 border-queen-500 border-t-transparent rounded-full animate-spin mx-auto mb-3"></div>
          <p class="text-light-500">Loading details...</p>
        </div>

        <div v-else-if="detailError" class="text-sm text-rose-600 dark:text-rose-400">
          {{ detailError }}
        </div>

        <template v-else-if="messageDetail">
          <!-- Status -->
          <div class="mb-6">
            <div class="mb-5">
              <span 
                class="badge text-sm"
                :class="{
                  'badge-cyber': messageDetail.status === 'pending',
                  'badge-crown': messageDetail.status === 'processing',
                  'badge-success': messageDetail.status === 'completed',
                  'badge-danger': messageDetail.status === 'dead_letter' || messageDetail.status === 'failed'
                }"
              >
                {{ messageDetail.status }}
              </span>
            </div>

            <div class="space-y-4">
              <div>
                <label class="text-xs font-medium text-light-500 block mb-1.5 uppercase tracking-wide">Queue / Partition</label>
                <p class="text-sm font-medium text-light-900 dark:text-light-100">
                  {{ messageDetail.queue }} / {{ messageDetail.partition }}
                </p>
              </div>
              
              <div>
                <label class="text-xs font-medium text-light-500 block mb-1.5 uppercase tracking-wide">Partition ID</label>
                <p class="text-xs font-mono text-light-700 dark:text-light-300 break-all">{{ messageDetail.partitionId }}</p>
              </div>
              
              <div>
                <label class="text-xs font-medium text-light-500 block mb-1.5 uppercase tracking-wide">Transaction ID</label>
                <p class="text-xs font-mono text-light-700 dark:text-light-300 break-all">{{ messageDetail.transactionId }}</p>
              </div>
              
              <div>
                <label class="text-xs font-medium text-light-500 block mb-1.5 uppercase tracking-wide">Created</label>
                <p class="text-sm text-light-700 dark:text-light-300">{{ formatDateTime(messageDetail.createdAt) }}</p>
              </div>
              
              <div v-if="messageDetail.traceId">
                <label class="text-xs font-medium text-light-500 block mb-1.5 uppercase tracking-wide">Trace ID</label>
                <p class="text-xs font-mono break-all text-light-700 dark:text-light-300">{{ messageDetail.traceId }}</p>
              </div>
              
              <div v-if="messageDetail.errorMessage">
                <label class="text-xs font-medium text-light-500 block mb-1.5 uppercase tracking-wide">Error Message</label>
                <p class="text-sm text-rose-600 dark:text-rose-400">{{ messageDetail.errorMessage }}</p>
              </div>
              
              <div v-if="messageDetail.retryCount">
                <label class="text-xs font-medium text-light-500 block mb-1.5 uppercase tracking-wide">Retry Count</label>
                <p class="text-sm text-light-700 dark:text-light-300">{{ messageDetail.retryCount }}</p>
              </div>
            </div>
          </div>

          <!-- Queue Config -->
          <div v-if="messageDetail.queueConfig" class="mb-6">
            <h4 class="text-sm font-semibold mb-3 text-light-900 dark:text-white">Queue Config</h4>
            <div class="bg-light-100 dark:bg-dark-300 rounded-lg p-4 grid grid-cols-2 gap-3 text-sm">
              <div>
                <span class="text-light-500">Lease Time:</span>
                <span class="font-medium ml-1">{{ messageDetail.queueConfig.leaseTime }}s</span>
              </div>
              <div>
                <span class="text-light-500">TTL:</span>
                <span class="font-medium ml-1">{{ messageDetail.queueConfig.ttl }}s</span>
              </div>
              <div>
                <span class="text-light-500">Retry Limit:</span>
                <span class="font-medium ml-1">{{ messageDetail.queueConfig.retryLimit }}</span>
              </div>
              <div>
                <span class="text-light-500">Retry Delay:</span>
                <span class="font-medium ml-1">{{ messageDetail.queueConfig.retryDelay }}ms</span>
              </div>
            </div>
          </div>

          <!-- Payload -->
          <div class="mb-6">
            <div class="flex items-center justify-between mb-3">
              <h4 class="text-sm font-semibold text-light-900 dark:text-white">Payload</h4>
              <button 
                @click="copyPayload"
                class="flex items-center gap-1.5 text-xs text-light-500 hover:text-queen-600 dark:hover:text-queen-400 transition-colors"
              >
                <svg v-if="!payloadCopied" class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z" />
                </svg>
                <svg v-else class="w-4 h-4 text-emerald-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
                </svg>
                {{ payloadCopied ? 'Copied!' : 'Copy' }}
              </button>
            </div>
            <div class="bg-dark-400 dark:bg-dark-500 rounded-lg p-4 overflow-x-auto">
              <pre class="text-sm font-mono whitespace-pre-wrap" v-html="highlightJson(messageDetail.payload)"></pre>
            </div>
          </div>

          <!-- Consumer Groups -->
          <div v-if="messageDetail.consumerGroups && messageDetail.consumerGroups.length > 0" class="mb-6">
            <h4 class="text-sm font-semibold mb-3 text-light-900 dark:text-white">Consumer Groups</h4>
            <div class="space-y-2">
              <div 
                v-for="group in messageDetail.consumerGroups" 
                :key="group.name"
                class="flex items-center justify-between p-3 bg-light-100 dark:bg-dark-300 rounded-lg"
              >
                <span class="text-sm font-medium">{{ group.name === '__QUEUE_MODE__' ? 'Queue Mode' : group.name }}</span>
                <span 
                  class="badge"
                  :class="group.consumed ? 'badge-success' : 'badge-cyber'"
                >
                  {{ group.consumed ? 'Consumed' : 'Pending' }}
                </span>
              </div>
            </div>
          </div>

          <!-- Actions -->
          <div class="space-y-2 pt-2">
            <!-- Completed message info -->
            <div v-if="messageDetail.status === 'completed'" class="bg-emerald-50 dark:bg-emerald-900/20 border border-emerald-200 dark:border-emerald-700/30 rounded-lg p-3 text-emerald-800 dark:text-emerald-200 text-sm">
              <div class="flex gap-2">
                <svg class="w-5 h-5 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
                <p>This message has been successfully consumed and acknowledged.</p>
              </div>
            </div>
            
            <button
              v-if="messageDetail.status === 'dead_letter'"
              @click="retryMessage"
              :disabled="actionLoading"
              class="btn btn-primary w-full"
            >
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
              </svg>
              Retry Message
            </button>
            
            <button
              v-if="messageDetail.status === 'pending'"
              @click="moveToDLQ"
              :disabled="actionLoading"
              class="btn btn-secondary w-full"
            >
              Move to Dead Letter Queue
            </button>
            
            <button
              @click="deleteMessage"
              :disabled="actionLoading"
              class="btn bg-rose-600 text-white hover:bg-rose-700 w-full"
            >
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
              </svg>
              Delete Message
            </button>
          </div>

          <div v-if="actionError" class="text-sm text-rose-600 dark:text-rose-400 mt-4">
            {{ actionError }}
          </div>
        </template>
      </div>
      </div>
      
      <!-- Backdrop -->
      <div 
        v-if="selectedMessage"
        class="fixed inset-0 bg-dark-500/50 backdrop-blur-sm z-40"
        @click="selectedMessage = null"
      ></div>
    </Teleport>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useRoute } from 'vue-router'
import { messages as messagesApi, queues as queuesApi } from '@/api'
import { formatNumber, formatDateTime } from '@/composables/useApi'
import { useRefresh } from '@/composables/useRefresh'

const route = useRoute()

// State
const messages = ref([])
const queues = ref([])
const queueMode = ref(null)
const loading = ref(true)

const searchQuery = ref('')
const filterQueue = ref('')
const filterStatus = ref('')
const filterFrom = ref('')
const filterTo = ref('')
const limit = ref(100)
const currentPage = ref(1)

const selectedMessage = ref(null)
const messageDetail = ref(null)
const detailLoading = ref(false)
const detailError = ref(null)
const actionLoading = ref(false)
const actionError = ref(null)
const payloadCopied = ref(false)

// Computed
const filteredMessages = computed(() => {
  let result = [...messages.value]
  
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase()
    result = result.filter(m => 
      m.transactionId?.toLowerCase().includes(query) ||
      m.partitionId?.toLowerCase().includes(query)
    )
  }
  
  return result
})

const hasActiveFilters = computed(() => {
  return searchQuery.value || filterQueue.value || filterStatus.value
})

// Helper to format Date to datetime-local input format
const formatDateTimeLocal = (date) => {
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hours = String(date.getHours()).padStart(2, '0')
  const minutes = String(date.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day}T${hours}:${minutes}`
}

// Helper to convert datetime-local to ISO string
const toISOString = (dateTimeLocal) => {
  if (!dateTimeLocal) return ''
  return new Date(dateTimeLocal).toISOString()
}

// Set time range preset
const setTimeRange = (hours) => {
  const now = new Date()
  const from = new Date(now.getTime() - hours * 60 * 60 * 1000)
  
  filterFrom.value = formatDateTimeLocal(from)
  filterTo.value = formatDateTimeLocal(now)
}

// Clear all filters
const clearFilters = () => {
  searchQuery.value = ''
  filterQueue.value = ''
  filterStatus.value = ''
  
  // Reset to default last 1 hour
  setTimeRange(1)
  
  applyFilters()
}

// Methods
const fetchMessages = async () => {
  loading.value = true
  try {
    const params = { 
      limit: limit.value,
      offset: (currentPage.value - 1) * limit.value
    }
    if (filterQueue.value) params.queue = filterQueue.value
    if (filterStatus.value) params.status = filterStatus.value
    if (filterFrom.value) params.from = toISOString(filterFrom.value)
    if (filterTo.value) params.to = toISOString(filterTo.value)
    
    const response = await messagesApi.list(params)
    messages.value = response.data?.messages || []
    queueMode.value = response.data?.mode || null
  } catch (err) {
    console.error('Failed to fetch messages:', err)
  } finally {
    loading.value = false
  }
}

const fetchQueues = async () => {
  try {
    const response = await queuesApi.list()
    queues.value = response.data?.queues || []
  } catch (err) {
    console.error('Failed to fetch queues:', err)
  }
}

const applyFilters = () => {
  currentPage.value = 1
  fetchMessages()
}

const prevPage = () => {
  if (currentPage.value > 1) {
    currentPage.value--
    fetchMessages()
  }
}

const nextPage = () => {
  currentPage.value++
  fetchMessages()
}

const selectMessage = async (message) => {
  selectedMessage.value = message
  detailLoading.value = true
  detailError.value = null
  messageDetail.value = null
  payloadCopied.value = false
  
  try {
    const response = await messagesApi.get(message.partitionId, message.transactionId)
    messageDetail.value = response.data
  } catch (err) {
    detailError.value = err.response?.data?.error || err.message
  } finally {
    detailLoading.value = false
  }
}

const retryMessage = async () => {
  if (!messageDetail.value) return
  
  actionLoading.value = true
  actionError.value = null
  
  try {
    await messagesApi.retry(messageDetail.value.partitionId, messageDetail.value.transactionId)
    selectedMessage.value = null
    fetchMessages()
  } catch (err) {
    actionError.value = err.response?.data?.error || err.message
  } finally {
    actionLoading.value = false
  }
}

const moveToDLQ = async () => {
  if (!messageDetail.value) return
  
  actionLoading.value = true
  actionError.value = null
  
  try {
    await messagesApi.moveToDLQ(messageDetail.value.partitionId, messageDetail.value.transactionId)
    selectedMessage.value = null
    fetchMessages()
  } catch (err) {
    actionError.value = err.response?.data?.error || err.message
  } finally {
    actionLoading.value = false
  }
}

const deleteMessage = async () => {
  if (!messageDetail.value) return
  if (!confirm('Are you sure you want to delete this message? This action cannot be undone.')) return
  
  actionLoading.value = true
  actionError.value = null
  
  try {
    await messagesApi.delete(messageDetail.value.partitionId, messageDetail.value.transactionId)
    selectedMessage.value = null
    fetchMessages()
  } catch (err) {
    actionError.value = err.response?.data?.error || err.message
  } finally {
    actionLoading.value = false
  }
}

const formatPayload = (payload) => {
  if (!payload) return 'null'
  if (typeof payload === 'string') {
    try {
      return JSON.stringify(JSON.parse(payload), null, 2)
    } catch {
      return payload
    }
  }
  return JSON.stringify(payload, null, 2)
}

const highlightJson = (payload) => {
  const json = formatPayload(payload)
  
  // Tokenize and highlight JSON properly
  let result = ''
  let i = 0
  
  const escapeHtml = (str) => {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  }
  
  while (i < json.length) {
    const char = json[i]
    
    // String
    if (char === '"') {
      let str = '"'
      i++
      while (i < json.length && json[i] !== '"') {
        if (json[i] === '\\' && i + 1 < json.length) {
          str += json[i] + json[i + 1]
          i += 2
        } else {
          str += json[i]
          i++
        }
      }
      str += '"'
      i++
      result += `<span class="text-emerald-400">${escapeHtml(str)}</span>`
    }
    // Number
    else if (char === '-' || (char >= '0' && char <= '9')) {
      let num = ''
      while (i < json.length && /[\d.eE+\-]/.test(json[i])) {
        num += json[i]
        i++
      }
      result += `<span class="text-amber-400">${num}</span>`
    }
    // true, false, null
    else if (json.slice(i, i + 4) === 'true') {
      result += '<span class="text-purple-400">true</span>'
      i += 4
    }
    else if (json.slice(i, i + 5) === 'false') {
      result += '<span class="text-purple-400">false</span>'
      i += 5
    }
    else if (json.slice(i, i + 4) === 'null') {
      result += '<span class="text-purple-400">null</span>'
      i += 4
    }
    // Braces and brackets
    else if (char === '{' || char === '}' || char === '[' || char === ']') {
      result += `<span class="text-light-500">${char}</span>`
      i++
    }
    // Colon
    else if (char === ':') {
      result += '<span class="text-light-400">:</span>'
      i++
    }
    // Everything else (whitespace, commas)
    else {
      result += escapeHtml(char)
      i++
    }
  }
  
  return result
}

const copyPayload = async () => {
  if (!messageDetail.value?.payload) return
  
  try {
    const text = formatPayload(messageDetail.value.payload)
    await navigator.clipboard.writeText(text)
    payloadCopied.value = true
    setTimeout(() => {
      payloadCopied.value = false
    }, 2000)
  } catch (err) {
    console.error('Failed to copy:', err)
  }
}

// Register for global refresh
useRefresh(fetchMessages)

// Initialize from query params and set default time range
onMounted(() => {
  if (route.query.queue) {
    filterQueue.value = route.query.queue
  }
  if (route.query.status) {
    filterStatus.value = route.query.status
  }
  
  // Set default time range to last 1 hour if not set via query
  if (!route.query.from && !route.query.to) {
    setTimeRange(1)
  }
  
  fetchQueues()
  fetchMessages()
})

// Watch for filter changes (auto-apply on queue/status change)
watch([filterQueue, filterStatus], () => {
  currentPage.value = 1
  fetchMessages()
})
</script>
