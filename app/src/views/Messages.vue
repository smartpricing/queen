<template>
  <div class="view-container animate-fade-in">

    <!-- Page head -->
    <div class="page-head">
      <div>
        <div class="eyebrow">Queue browser</div>
        <h1><span class="accent">Messages</span></h1>
        <p>Browse, search and inspect messages across all queues.</p>
      </div>
    </div>

    <!-- Mode Indicator (for Bus Mode) -->
    <div v-if="queueMode && queueMode.type === 'bus'" class="card" style="padding:10px 14px; margin-bottom:16px;">
      <div style="display:flex; align-items:center; gap:8px; font-size:13px;">
        <svg style="width:18px; height:18px; color:#a78bfa;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
        </svg>
        <span style="font-weight:600; color:var(--text-hi);">Bus Mode Active</span>
        <span class="chip chip-warn">{{ queueMode.busGroupsCount }} consumer group(s)</span>
      </div>
    </div>

    <!-- Filters -->
    <div class="card" style="margin-bottom:16px;">
      <div class="card-header">
        <h3>Filters</h3>
      </div>
      <div class="card-body" style="display:flex; flex-direction:column; gap:14px;">
        <!-- First Row: Search, Queue, Status, Limit -->
        <div style="display:grid; grid-template-columns:repeat(auto-fill, minmax(150px, 1fr)); gap:12px; align-items:end;">
          <div>
            <label class="label-xs" style="display:block; margin-bottom:6px;">Search</label>
            <div style="position:relative;">
              <input
                v-model="searchQuery"
                type="text"
                placeholder="Search by transaction ID..."
                class="input"
                style="padding-left:34px;"
              />
              <svg
                style="position:absolute; left:10px; top:50%; transform:translateY(-50%); width:15px; height:15px; color:var(--text-low);"
                fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
              >
                <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
              </svg>
            </div>
          </div>

          <div>
            <label class="label-xs" style="display:block; margin-bottom:6px;">Queue</label>
            <select v-model="filterQueue" class="input">
              <option value="">All Queues</option>
              <option v-for="q in queues" :key="q.name" :value="q.name">
                {{ q.name }}
              </option>
            </select>
          </div>

          <div>
            <label class="label-xs" style="display:block; margin-bottom:6px;">Partition Name</label>
            <input
              v-model="filterPartition"
              type="text"
              placeholder="Filter by name..."
              class="input"
            />
          </div>

          <div>
            <label class="label-xs" style="display:block; margin-bottom:6px;">Status</label>
            <select v-model="filterStatus" class="input">
              <option value="">All Status</option>
              <option value="pending">Pending</option>
              <option value="processing">Processing</option>
              <option value="completed">Completed</option>
              <option value="dead_letter">Dead Letter</option>
            </select>
          </div>

          <div>
            <label class="label-xs" style="display:block; margin-bottom:6px;">Limit</label>
            <select v-model="limit" class="input">
              <option :value="50">50 messages</option>
              <option :value="100">100 messages</option>
              <option :value="200">200 messages</option>
              <option :value="500">500 messages</option>
            </select>
          </div>
        </div>

        <!-- Second Row: Date Range and Actions -->
        <div style="display:flex; flex-wrap:wrap; align-items:end; gap:12px;">
          <div style="flex:1; min-width:180px; max-width:260px;">
            <label class="label-xs" style="display:block; margin-bottom:6px;">From</label>
            <input
              v-model="filterFrom"
              type="datetime-local"
              class="input"
              style="font-size:13px;"
            />
          </div>

          <div style="flex:1; min-width:180px; max-width:260px;">
            <label class="label-xs" style="display:block; margin-bottom:6px;">To</label>
            <input
              v-model="filterTo"
              type="datetime-local"
              class="input"
              style="font-size:13px;"
            />
          </div>

          <!-- Quick Time Range Buttons -->
          <div style="display:flex; flex-wrap:wrap; gap:8px;">
            <button @click="setTimeRange(1)" class="btn btn-ghost" style="font-size:12px;">1h</button>
            <button @click="setTimeRange(24)" class="btn btn-ghost" style="font-size:12px;">24h</button>
            <button @click="setTimeRange(168)" class="btn btn-ghost" style="font-size:12px;">7d</button>
            <button @click="applyFilters" class="btn btn-primary" style="font-size:12px;">Apply</button>
            <button
              v-if="hasActiveFilters"
              @click="clearFilters"
              class="btn btn-ghost" style="font-size:12px;"
            >
              Clear
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Messages list -->
    <div class="card">
      <div class="card-header">
        <h3>Messages</h3>
        <span class="chip chip-ice">{{ formatNumber(messages.length) }} loaded</span>
        <span class="muted">Page <span class="font-mono tabular-nums">{{ currentPage }}</span></span>
      </div>

      <div style="overflow-x:auto;">
        <table class="t">
          <thead>
            <tr>
              <th>Queue</th>
              <th class="hidden xl:table-cell">Partition ID</th>
              <th class="hidden lg:table-cell">Partition</th>
              <th>Transaction ID</th>
              <th style="text-align:right;">Created</th>
              <th style="text-align:right;">Status</th>
            </tr>
          </thead>
          <tbody>
            <template v-if="loading">
              <tr v-for="i in 10" :key="i">
                <td><div class="skeleton" style="height:16px; width:96px;" /></td>
                <td class="hidden xl:table-cell"><div class="skeleton" style="height:16px; width:128px;" /></td>
                <td class="hidden lg:table-cell"><div class="skeleton" style="height:16px; width:48px;" /></td>
                <td><div class="skeleton" style="height:16px; width:160px;" /></td>
                <td><div class="skeleton" style="height:16px; width:112px;" /></td>
                <td><div class="skeleton" style="height:16px; width:80px;" /></td>
              </tr>
            </template>
            <template v-else-if="filteredMessages.length > 0">
              <tr
                v-for="message in filteredMessages"
                :key="message.id"
                style="cursor:pointer;"
                @click="selectMessage(message)"
              >
                <td>
                  <div style="font-size:13px; font-weight:500; color:var(--text-hi);">{{ message.queue }}</div>
                  <div class="lg:hidden" style="font-size:11px; color:var(--text-low); margin-top:2px;">
                    {{ message.partition }}
                  </div>
                </td>
                <td class="hidden xl:table-cell">
                  <div class="font-mono" style="font-size:11px; color:var(--text-mid); word-break:break-all; user-select:all;">
                    {{ message.partitionId }}
                  </div>
                </td>
                <td class="hidden lg:table-cell">
                  <span style="font-size:12px; color:var(--text-mid);">{{ message.partition }}</span>
                </td>
                <td>
                  <div class="font-mono" style="font-size:11px; color:var(--text-hi); word-break:break-all; user-select:all;">
                    {{ message.transactionId }}
                  </div>
                </td>
                <td class="font-mono tabular-nums" style="text-align:right; font-size:12px; color:var(--text-mid); white-space:nowrap;">
                  {{ formatDateTime(message.createdAt) }}
                </td>
                <td style="text-align:right;">
                  <div style="display:flex; flex-direction:column; align-items:flex-end; gap:4px;">
                    <span
                      class="chip"
                      :class="{
                        'chip-ice': message.status === 'pending',
                        'chip-warn': message.status === 'processing',
                        'chip-ok': message.status === 'completed',
                        'chip-bad': message.status === 'dead_letter' || message.status === 'failed'
                      }"
                    >
                      {{ message.status }}
                    </span>
                    <div v-if="message.busStatus && message.busStatus.totalGroups > 0" style="font-size:11px; color:var(--text-low);">
                      {{ message.busStatus.consumedBy }}/{{ message.busStatus.totalGroups }} groups
                    </div>
                  </div>
                </td>
              </tr>
            </template>
            <tr v-else>
              <td colspan="6" style="text-align:center; padding:48px 16px;">
                <svg style="width:48px; height:48px; margin:0 auto 12px; color:var(--text-low);" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M21.75 6.75v10.5a2.25 2.25 0 01-2.25 2.25h-15a2.25 2.25 0 01-2.25-2.25V6.75m19.5 0A2.25 2.25 0 0019.5 4.5h-15a2.25 2.25 0 00-2.25 2.25m19.5 0v.243a2.25 2.25 0 01-1.07 1.916l-7.5 4.615a2.25 2.25 0 01-2.36 0L3.32 8.91a2.25 2.25 0 01-1.07-1.916V6.75" />
                </svg>
                <p style="color:var(--text-mid);">No messages found</p>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- Pagination -->
      <div style="padding:14px 16px; border-top:1px solid var(--bd); display:flex; align-items:center; justify-content:space-between;">
        <div style="font-size:13px; color:var(--text-mid);">
          Page <span class="font-mono tabular-nums">{{ currentPage }}</span>
        </div>
        <div style="display:flex; gap:8px;">
          <button
            @click="prevPage"
            :disabled="currentPage === 1"
            class="btn btn-ghost" style="padding:6px 10px;"
          >
            <svg style="width:16px; height:16px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
            </svg>
          </button>
          <button
            @click="nextPage"
            :disabled="messages.length < limit"
            class="btn btn-ghost" style="padding:6px 10px;"
          >
            <svg style="width:16px; height:16px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
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
        style="position:fixed; top:0; right:0; bottom:0; width:100%; max-width:640px; z-index:50; overflow-y:auto; border-left:1px solid var(--bd); background:linear-gradient(180deg, rgba(14,14,18,.97), rgba(10,10,14,.97)); backdrop-filter:blur(16px);"
      >
      <div style="padding:20px 24px;">
        <!-- Header -->
        <div style="display:flex; align-items:flex-start; justify-content:space-between; margin-bottom:20px; padding-bottom:16px; border-bottom:1px solid var(--bd);">
          <div style="flex:1; min-width:0;">
            <h3 style="font-size:15px; font-weight:700; margin-bottom:4px; color:var(--text-hi);">Message Details</h3>
            <p class="font-mono" style="font-size:11px; color:var(--text-low); word-break:break-all;">
              {{ messageDetail?.transactionId }}
            </p>
          </div>
          <button
            @click="selectedMessage = null"
            class="btn btn-ghost btn-icon"
          >
            <svg style="width:18px; height:18px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>

        <div v-if="detailLoading" style="text-align:center; padding:48px 0;">
          <div class="spinner" style="margin:0 auto 12px;"></div>
          <p style="color:var(--text-low);">Loading details...</p>
        </div>

        <div v-else-if="detailError" style="font-size:13px; color:#fb7185;">
          {{ detailError }}
        </div>

        <template v-else-if="messageDetail">
          <!-- Status -->
          <div style="margin-bottom:24px;">
            <div style="margin-bottom:20px;">
              <span
                class="chip"
                style="font-size:12px;"
                :class="{
                  'chip-ice': messageDetail.status === 'pending',
                  'chip-warn': messageDetail.status === 'processing',
                  'chip-ok': messageDetail.status === 'completed',
                  'chip-bad': messageDetail.status === 'dead_letter' || messageDetail.status === 'failed'
                }"
              >
                {{ messageDetail.status }}
              </span>
            </div>

            <div style="display:flex; flex-direction:column; gap:16px;">
              <div>
                <label class="label-xs" style="display:block; margin-bottom:6px;">Queue / Partition</label>
                <p style="font-size:13px; font-weight:500; color:var(--text-hi);">
                  {{ messageDetail.queue }} / {{ messageDetail.partition }}
                </p>
              </div>

              <div>
                <label class="label-xs" style="display:block; margin-bottom:6px;">Partition ID</label>
                <p class="font-mono" style="font-size:11px; color:var(--text-mid); word-break:break-all;">{{ messageDetail.partitionId }}</p>
              </div>

              <div>
                <label class="label-xs" style="display:block; margin-bottom:6px;">Transaction ID</label>
                <p class="font-mono" style="font-size:11px; color:var(--text-mid); word-break:break-all;">{{ messageDetail.transactionId }}</p>
              </div>

              <div>
                <label class="label-xs" style="display:block; margin-bottom:6px;">Created</label>
                <p style="font-size:13px; color:var(--text-mid);">{{ formatDateTime(messageDetail.createdAt) }}</p>
              </div>

              <div v-if="messageDetail.traceId">
                <label class="label-xs" style="display:block; margin-bottom:6px;">Trace ID</label>
                <p class="font-mono" style="font-size:11px; color:var(--text-mid); word-break:break-all;">{{ messageDetail.traceId }}</p>
              </div>

              <div v-if="messageDetail.errorMessage">
                <label class="label-xs" style="display:block; margin-bottom:6px;">Error Message</label>
                <p style="font-size:13px; color:#fb7185;">{{ messageDetail.errorMessage }}</p>
              </div>

              <div v-if="messageDetail.retryCount">
                <label class="label-xs" style="display:block; margin-bottom:6px;">Retry Count</label>
                <p class="font-mono tabular-nums" style="font-size:13px; color:var(--text-mid);">{{ messageDetail.retryCount }}</p>
              </div>
            </div>
          </div>

          <!-- Queue Config -->
          <div v-if="messageDetail.queueConfig" style="margin-bottom:24px;">
            <h4 style="font-size:13px; font-weight:600; margin-bottom:12px; color:var(--text-hi);">Queue Config</h4>
            <div class="card" style="padding:14px 16px;">
              <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px; font-size:13px;">
                <div>
                  <span style="color:var(--text-low);">Lease Time:</span>
                  <span class="font-mono tabular-nums" style="font-weight:500; margin-left:4px; color:var(--text-hi);">{{ messageDetail.queueConfig.leaseTime }}s</span>
                </div>
                <div>
                  <span style="color:var(--text-low);">TTL:</span>
                  <span class="font-mono tabular-nums" style="font-weight:500; margin-left:4px; color:var(--text-hi);">{{ messageDetail.queueConfig.ttl }}s</span>
                </div>
                <div>
                  <span style="color:var(--text-low);">Retry Limit:</span>
                  <span class="font-mono tabular-nums" style="font-weight:500; margin-left:4px; color:var(--text-hi);">{{ messageDetail.queueConfig.retryLimit }}</span>
                </div>
                <div>
                  <span style="color:var(--text-low);">Retry Delay:</span>
                  <span class="font-mono tabular-nums" style="font-weight:500; margin-left:4px; color:var(--text-hi);">{{ messageDetail.queueConfig.retryDelay }}ms</span>
                </div>
              </div>
            </div>
          </div>

          <!-- Payload -->
          <div style="margin-bottom:24px;">
            <div style="display:flex; align-items:center; justify-content:space-between; margin-bottom:12px;">
              <h4 style="font-size:13px; font-weight:600; color:var(--text-hi);">Payload</h4>
              <button
                @click="copyPayload"
                class="btn btn-ghost" style="padding:4px 8px; font-size:11px; gap:4px;"
              >
                <svg v-if="!payloadCopied" style="width:14px; height:14px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z" />
                </svg>
                <svg v-else style="width:14px; height:14px; color:#34d399;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
                </svg>
                {{ payloadCopied ? 'Copied!' : 'Copy' }}
              </button>
            </div>
            <div style="background:rgba(0,0,0,.35); border:1px solid var(--bd); border-radius:10px; padding:14px 16px; overflow-x:auto;">
              <pre class="font-mono" style="font-size:12px; white-space:pre-wrap; margin:0;" v-html="highlightJson(messageDetail.payload)"></pre>
            </div>
          </div>

          <!-- Consumer Groups -->
          <div v-if="messageDetail.consumerGroups && messageDetail.consumerGroups.length > 0" style="margin-bottom:24px;">
            <h4 style="font-size:13px; font-weight:600; margin-bottom:12px; color:var(--text-hi);">Consumer Groups</h4>
            <div style="display:flex; flex-direction:column; gap:8px;">
              <div
                v-for="group in messageDetail.consumerGroups"
                :key="group.name"
                class="card"
                style="display:flex; align-items:center; justify-content:space-between; padding:10px 14px;"
              >
                <span style="font-size:13px; font-weight:500; color:var(--text-hi);">{{ group.name === '__QUEUE_MODE__' ? 'Queue Mode' : group.name }}</span>
                <span
                  class="chip"
                  :class="group.consumed ? 'chip-ok' : 'chip-ice'"
                >
                  {{ group.consumed ? 'Consumed' : 'Pending' }}
                </span>
              </div>
            </div>
          </div>

          <!-- Actions -->
          <div style="display:flex; flex-direction:column; gap:8px; padding-top:8px;">
            <!-- Completed message info -->
            <div v-if="messageDetail.status === 'completed'" class="card" style="padding:12px 14px; border-color:rgba(52,211,153,.2);">
              <div style="display:flex; gap:8px; align-items:center; font-size:13px; color:#34d399;">
                <svg style="width:18px; height:18px; flex-shrink:0;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
                <p>This message has been successfully consumed and acknowledged.</p>
              </div>
            </div>

            <button
              v-if="messageDetail.status === 'dead_letter'"
              @click="retryMessage"
              :disabled="actionLoading"
              class="btn btn-primary" style="width:100%; justify-content:center;"
            >
              <svg style="width:16px; height:16px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
              </svg>
              Retry Message
            </button>

            <button
              v-if="messageDetail.status === 'pending'"
              @click="moveToDLQ"
              :disabled="actionLoading"
              class="btn btn-ghost" style="width:100%; justify-content:center;"
            >
              Move to Dead Letter Queue
            </button>

            <button
              @click="deleteMessage"
              :disabled="actionLoading"
              class="btn btn-danger" style="width:100%; justify-content:center;"
            >
              <svg style="width:16px; height:16px;" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
              </svg>
              Delete Message
            </button>
          </div>

          <div v-if="actionError" style="font-size:13px; color:#fb7185; margin-top:16px;">
            {{ actionError }}
          </div>
        </template>
      </div>
      </div>

      <!-- Backdrop -->
      <div
        v-if="selectedMessage"
        style="position:fixed; inset:0; z-index:40; background:rgba(0,0,0,.5); backdrop-filter:blur(4px);"
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
const filterPartition = ref('')
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
  return searchQuery.value || filterQueue.value || filterPartition.value || filterStatus.value
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
  filterPartition.value = ''
  filterStatus.value = ''
  
  // Reset to default last 1 hour
  setTimeRange(1)
  
  applyFilters()
}

// Methods
const fetchMessages = async () => {
  // Only show loading skeleton if we don't have data yet (smooth background refresh)
  if (!messages.value.length) loading.value = true
  try {
    const params = { 
      limit: limit.value,
      offset: (currentPage.value - 1) * limit.value
    }
    if (filterQueue.value) params.queue = filterQueue.value
    if (filterPartition.value) params.partition = filterPartition.value
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
      result += `<span style="color:var(--text-low)">${char}</span>`
      i++
    }
    // Colon
    else if (char === ':') {
      result += '<span style="color:var(--text-faint)">:</span>'
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
  if (route.query.partition) {
    filterPartition.value = route.query.partition
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
watch([filterQueue, filterPartition, filterStatus], () => {
  currentPage.value = 1
  fetchMessages()
})
</script>
