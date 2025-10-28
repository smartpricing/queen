<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <LoadingSpinner v-if="loading && !queueData" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading queue:</strong> {{ error }}</p>
        </div>

        <template v-else-if="queueData">
          <!-- Queue Header Card -->
          <div class="header-card">
            <div class="flex items-center justify-between mb-3">
              <div class="flex items-center gap-3">
                <button @click="goBack" class="p-2 hover:bg-gray-100 dark:hover:bg-gray-800/50 rounded-lg transition-colors">
                  <svg class="w-5 h-5 text-gray-600 dark:text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                    <path stroke-linecap="round" stroke-linejoin="round" d="M10 19l-7-7m0 0l7-7m-7 7h18" />
                  </svg>
                </button>
                <h2 class="text-xl font-bold text-gray-900 dark:text-white tracking-tight">{{ statusData?.queue?.name }}</h2>
              </div>
              
              <div class="flex gap-2">
                <button @click="showPushModal = true" class="btn btn-primary">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4v16m8-8H4" />
                  </svg>
                  Push Message
                </button>
                <button @click="confirmClear" class="btn btn-secondary">
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                  </svg>
                  Clear Queue
                </button>
              </div>
            </div>
            
            <div class="flex flex-wrap gap-x-6 gap-y-1 text-sm text-gray-600 dark:text-gray-400 mb-1">
              <span><span class="text-gray-500 dark:text-gray-500">Namespace:</span> {{ statusData?.queue?.namespace || '-' }}</span>
              <span><span class="text-gray-500 dark:text-gray-500">Task:</span> {{ statusData?.queue?.task || '-' }}</span>
              <span><span class="text-gray-500 dark:text-gray-500">Priority:</span> {{ statusData?.queue?.priority || 0 }}</span>
              <span class="text-xs text-gray-500">Created: {{ formatDate(statusData?.queue?.createdAt) }}</span>
            </div>
          </div>

          <!-- Status Metrics -->
          <div class="metrics-grid">
            <div class="metric-card-compact">
              <span class="metric-label-sm">PENDING</span>
              <div class="metric-value-sm text-orange-600 dark:text-orange-400">{{ formatNumber(calculatedPending) }}</div>
            </div>
            <div class="metric-card-compact">
              <span class="metric-label-sm">PROCESSING</span>
              <div class="metric-value-sm text-blue-600 dark:text-blue-400">{{ formatNumber(statusData?.totals?.messages?.processing || 0) }}</div>
            </div>
            <div class="metric-card-compact">
              <span class="metric-label-sm">COMPLETED</span>
              <div class="metric-value-sm text-emerald-600 dark:text-emerald-400">{{ formatNumber(statusData?.totals?.messages?.completed || 0) }}</div>
            </div>
            <div class="metric-card-compact">
              <span class="metric-label-sm">FAILED</span>
              <div class="metric-value-sm text-red-600 dark:text-red-400">{{ formatNumber(statusData?.totals?.messages?.failed || 0) }}</div>
            </div>
          </div>

          <!-- Queue Configuration -->
          <div class="config-card">
            <h3 class="text-sm font-semibold text-gray-900 dark:text-white mb-4 tracking-tight">Queue Configuration</h3>
            <div class="grid grid-cols-2 md:grid-cols-3 gap-x-6 gap-y-3 text-sm">
              <div>
                <span class="text-gray-500 dark:text-gray-400 block mb-0.5">Lease Time</span>
                <span class="font-semibold">{{ statusData?.queue?.config?.leaseTime || 0 }}s</span>
              </div>
              <div>
                <span class="text-gray-500 dark:text-gray-400 block mb-0.5">TTL</span>
                <span class="font-semibold">{{ formatDuration((statusData?.queue?.config?.ttl || 0) * 1000) }}</span>
              </div>
              <div>
                <span class="text-gray-500 dark:text-gray-400 block mb-0.5">Max Queue Size</span>
                <span class="font-semibold">{{ formatNumber(statusData?.queue?.config?.maxQueueSize || 0) }}</span>
              </div>
              <div>
                <span class="text-gray-500 dark:text-gray-400 block mb-0.5">Retry Limit</span>
                <span class="font-semibold">{{ statusData?.queue?.config?.retryLimit || 0 }}</span>
              </div>
              <div>
                <span class="text-gray-500 dark:text-gray-400 block mb-0.5">Retry Delay</span>
                <span class="font-semibold">{{ statusData?.queue?.config?.retryDelay || 0 }}ms</span>
              </div>
              <div>
                <span class="text-gray-500 dark:text-gray-400 block mb-0.5">DLQ Enabled</span>
                <span class="font-semibold">{{ statusData?.queue?.config?.deadLetterQueue ? 'Yes' : 'No' }}</span>
              </div>
            </div>
          </div>

          <!-- Partitions Table -->
          <div class="partition-section">
            <div class="flex items-center justify-between mb-2">
              <div class="flex items-center gap-2">
                <div class="w-7 h-7 rounded-lg bg-gradient-to-br from-indigo-500/10 to-violet-500/20 flex items-center justify-center">
                  <svg class="w-4 h-4 text-indigo-600 dark:text-indigo-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4m0 5c0 2.21-3.582 4-8 4s-8-1.79-8-4" />
                  </svg>
                </div>
                <h3 class="text-base font-semibold text-gray-900 dark:text-gray-100">Partitions</h3>
              </div>
              <input
                v-model="partitionSearch"
                type="text"
                placeholder="Search partitions..."
                class="input w-48"
              />
            </div>
            
            <div class="table-container scrollbar-thin">
              <table class="table">
                <thead>
                  <tr>
                    <th @click="sortPartitions('name')" class="cursor-pointer hover:text-blue-600 dark:hover:text-blue-400 transition-colors">
                      <div class="flex items-center gap-1">
                        Partition
                        <svg class="w-3 h-3 transition-transform" :class="getPartitionSortClass('name')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                        </svg>
                      </div>
                    </th>
                    <th @click="sortPartitions('pending')" class="text-right cursor-pointer hover:text-blue-600 dark:hover:text-blue-400 transition-colors">
                      <div class="flex items-center justify-end gap-1">
                        Pending
                        <svg class="w-3 h-3 transition-transform" :class="getPartitionSortClass('pending')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                        </svg>
                      </div>
                    </th>
                    <th @click="sortPartitions('processing')" class="text-right cursor-pointer hover:text-blue-600 dark:hover:text-blue-400 transition-colors">
                      <div class="flex items-center justify-end gap-1">
                        Processing
                        <svg class="w-3 h-3 transition-transform" :class="getPartitionSortClass('processing')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                        </svg>
                      </div>
                    </th>
                    <th @click="sortPartitions('completed')" class="text-right cursor-pointer hover:text-blue-600 dark:hover:text-blue-400 transition-colors">
                      <div class="flex items-center justify-end gap-1">
                        Completed
                        <svg class="w-3 h-3 transition-transform" :class="getPartitionSortClass('completed')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                        </svg>
                      </div>
                    </th>
                    <th @click="sortPartitions('failed')" class="text-right cursor-pointer hover:text-blue-600 dark:hover:text-blue-400 transition-colors">
                      <div class="flex items-center justify-end gap-1">
                        Failed
                        <svg class="w-3 h-3 transition-transform" :class="getPartitionSortClass('failed')" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 9l-7 7-7-7" />
                        </svg>
                      </div>
                    </th>
                    <th class="text-right">Consumed</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="partition in sortedPartitions" :key="partition.id">
                    <td>
                      <div class="font-medium">{{ partition.name }}</div>
                      <div v-if="partition.lastActivity" class="text-xs text-gray-500 dark:text-gray-400">
                        Last: {{ formatTime(partition.lastActivity) }}
                      </div>
                    </td>
                    <td class="text-right font-medium">{{ formatNumber(partition.messages?.pending || 0) }}</td>
                    <td class="text-right font-medium">{{ formatNumber(partition.messages?.processing || 0) }}</td>
                    <td class="text-right font-medium text-green-600 dark:text-green-400">{{ formatNumber(partition.messages?.completed || 0) }}</td>
                    <td class="text-right font-medium text-red-600 dark:text-red-400">{{ formatNumber(partition.messages?.failed || 0) }}</td>
                    <td class="text-right">
                      <div class="text-sm">{{ formatNumber(partition.cursor?.totalConsumed || 0) }}</div>
                      <div class="text-xs text-gray-500 dark:text-gray-400">
                        {{ partition.cursor?.batchesConsumed || 0 }} batches
                      </div>
                    </td>
                  </tr>
                </tbody>
              </table>
              
              <div v-if="!sortedPartitions.length" class="text-center py-8 text-gray-500 text-sm">
                No partitions found
              </div>
            </div>
          </div>
        </template>

        <!-- Push Message Modal -->
        <PushMessageModal
          :is-open="showPushModal"
          :queue-name="queueName"
          @close="showPushModal = false"
          @pushed="onMessagePushed"
        />

        <!-- Clear Queue Confirmation -->
        <ConfirmDialog
          :is-open="showClearConfirm"
          title="Clear Queue"
          :message="`Are you sure you want to clear all messages from '${queueName}'? This action cannot be undone.`"
          confirm-text="Clear Queue"
          confirm-class="btn-danger"
          @confirm="clearQueue"
          @cancel="showClearConfirm = false"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { queuesApi } from '../api/queues';
import { analyticsApi } from '../api/analytics';
import { formatNumber, formatDate, formatTime, formatDuration } from '../utils/formatters';

import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import ConfirmDialog from '../components/common/ConfirmDialog.vue';
import PushMessageModal from '../components/queue-detail/PushMessageModal.vue';

const route = useRoute();
const router = useRouter();
const queueName = computed(() => route.params.queueName);

const loading = ref(false);
const error = ref(null);
const queueData = ref(null);
const statusData = ref(null);
const partitionSearch = ref('');
const partitionSortColumn = ref('name');
const partitionSortDirection = ref('asc');

const showPushModal = ref(false);
const showClearConfirm = ref(false);

const calculatedPending = computed(() => {
  if (!statusData.value?.totals?.messages) return 0;
  
  const total = queueData.value?.totals?.total || 0;
  const completed = statusData.value.totals.messages.completed || 0;
  const failed = statusData.value.totals.messages.failed || 0;
  const processing = statusData.value.totals.messages.processing || 0;
  
  return Math.max(0, total - completed - failed - processing);
});

const sortedPartitions = computed(() => {
  let partitions = statusData.value?.partitions || [];
  
  // Filter by search
  if (partitionSearch.value) {
    const search = partitionSearch.value.toLowerCase();
    partitions = partitions.filter(p => p.name.toLowerCase().includes(search));
  }
  
  // Sort
  return [...partitions].sort((a, b) => {
    let aVal, bVal;
    
    switch (partitionSortColumn.value) {
      case 'name':
        aVal = a.name.toLowerCase();
        bVal = b.name.toLowerCase();
        break;
      case 'pending':
        aVal = a.messages?.pending || 0;
        bVal = b.messages?.pending || 0;
        break;
      case 'processing':
        aVal = a.messages?.processing || 0;
        bVal = b.messages?.processing || 0;
        break;
      case 'completed':
        aVal = a.messages?.completed || 0;
        bVal = b.messages?.completed || 0;
        break;
      case 'failed':
        aVal = a.messages?.failed || 0;
        bVal = b.messages?.failed || 0;
        break;
      default:
        return 0;
    }
    
    if (aVal < bVal) return partitionSortDirection.value === 'asc' ? -1 : 1;
    if (aVal > bVal) return partitionSortDirection.value === 'asc' ? 1 : -1;
    return 0;
  });
});

function sortPartitions(column) {
  if (partitionSortColumn.value === column) {
    partitionSortDirection.value = partitionSortDirection.value === 'asc' ? 'desc' : 'asc';
  } else {
    partitionSortColumn.value = column;
    partitionSortDirection.value = 'asc';
  }
}

function getPartitionSortClass(column) {
  if (partitionSortColumn.value !== column) {
    return 'opacity-30';
  }
  return partitionSortDirection.value === 'asc' ? '' : 'rotate-180';
}

function goBack() {
  router.push('/queues');
}

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    const [queueRes, statusRes] = await Promise.all([
      queuesApi.getQueue(queueName.value),
      analyticsApi.getQueueDetail(queueName.value),
    ]);
    
    queueData.value = queueRes.data;
    statusData.value = statusRes.data;
  } catch (err) {
    error.value = err.message;
    console.error('Queue detail error:', err);
  } finally {
    loading.value = false;
  }
}

function confirmClear() {
  showClearConfirm.value = true;
}

async function clearQueue() {
  try {
    await queuesApi.clearQueue(queueName.value);
    showClearConfirm.value = false;
    await loadData();
  } catch (err) {
    error.value = err.message;
  }
}

async function onMessagePushed() {
  await loadData();
}

onMounted(() => {
  loadData();
});
</script>

<style scoped>

.header-section {
  background: transparent;
  padding: 0;
}

.config-section {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  transition: all 0.3s ease;
}

.dark .config-section {
  background: #0a0d14;
}

.config-section:hover {
  background: #fafafa;
}

.dark .config-section:hover {
  background: #0d1117;
}

.partition-section {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
}

.dark .partition-section {
  background: #0a0d14;
}

/* Flat inputs */
.partition-section :deep(.input) {
  background: transparent;
  border: 1px solid rgba(156, 163, 175, 0.15);
  transition: all 0.2s ease;
}

.partition-section :deep(.input:hover) {
  border-color: rgba(156, 163, 175, 0.25);
}

.partition-section :deep(.input:focus) {
  background: rgba(59, 130, 246, 0.02);
  border-color: rgba(59, 130, 246, 0.4);
  box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.05);
}

.dark .partition-section :deep(.input) {
  border-color: rgba(156, 163, 175, 0.1);
}

.dark .partition-section :deep(.input:focus) {
  background: rgba(59, 130, 246, 0.03);
  border-color: rgba(59, 130, 246, 0.5);
  box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.08);
}

/* Flat table styling */
.partition-section :deep(.table) {
  border-collapse: separate;
  border-spacing: 0;
}

.partition-section :deep(.table thead) {
  background: transparent;
  border-bottom: 1px solid rgba(156, 163, 175, 0.08);
}

.dark .partition-section :deep(.table thead) {
  border-bottom-color: rgba(156, 163, 175, 0.1);
}

.partition-section :deep(.table th) {
  padding: 1rem 1rem;
  font-weight: 600;
  letter-spacing: 0.05em;
}

.partition-section :deep(.table tbody tr) {
  border: none;
  transition: all 0.15s ease;
}

.partition-section :deep(.table tbody tr:nth-child(even)) {
  background: rgba(0, 0, 0, 0.015);
}

.dark .partition-section :deep(.table tbody tr:nth-child(even)) {
  background: rgba(255, 255, 255, 0.02);
}

.partition-section :deep(.table tbody tr:hover) {
  background: rgba(59, 130, 246, 0.03);
  box-shadow: inset 3px 0 0 0 rgba(59, 130, 246, 0.6);
}

.dark .partition-section :deep(.table tbody tr:hover) {
  background: rgba(59, 130, 246, 0.05);
  box-shadow: inset 3px 0 0 0 rgba(59, 130, 246, 0.8);
}

.partition-section :deep(.table td) {
  padding: 0.875rem 1rem;
  border: none;
}

.metric-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  transition: all 0.3s ease;
}

.dark .metric-flat {
  background: #0a0d14;
}

.metric-flat:hover {
  background: #fafafa;
}

.dark .metric-flat:hover {
  background: #0d1117;
}

.metric-icon-flat {
  width: 2rem;
  height: 2rem;
  border-radius: 0.5rem;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.3s ease;
}

.metric-flat:hover .metric-icon-flat {
  transform: scale(1.05);
}

.metric-value-flat {
  font-size: 1.5rem;
  font-weight: 700;
  line-height: 1.1;
  margin-top: 0.125rem;
  background: linear-gradient(135deg, #f43f5e 0%, #ec4899 50%, #a855f7 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  letter-spacing: -0.02em;
}

.error-flat {
  background: transparent;
  color: #dc2626;
  font-size: 0.875rem;
  padding: 1rem;
}

.dark .error-flat {
  color: #fca5a5;
}
</style>
