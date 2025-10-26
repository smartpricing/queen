<template>
  <div class="page-flat">
    <div class="py-4 px-3">
      <div class="space-y-2.5">
        <!-- Info Banner (shown when API needs restart) -->
        <div v-if="apiNeedsRestart" class="warning-banner">
          <div class="flex gap-3">
            <svg class="w-5 h-5 flex-shrink-0 mt-0.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
            </svg>
            <div class="text-sm">
              <p class="font-semibold mb-1">API Update Required</p>
              <p class="text-yellow-800 dark:text-yellow-200">
                The messages API has been fixed. Please restart the Queen server for changes to take effect.
              </p>
            </div>
          </div>
        </div>

        <!-- Filters -->
        <div class="filter-flat">
          <MessageFilters
          v-model:search="searchQuery"
          v-model:queue="queueFilter"
          v-model:status="statusFilter"
          v-model:from="fromFilter"
          v-model:to="toFilter"
          :queues="queues"
          />
        </div>

        <LoadingSpinner v-if="loading && !messages.length" />

        <div v-else-if="error" class="error-flat">
          <p><strong>Error loading messages:</strong> {{ error }}</p>
        </div>

        <!-- Messages Table -->
        <div v-else class="table-flat">
          <div class="table-container scrollbar-thin">
            <table class="table">
              <thead>
                <tr>
                  <th>Transaction ID</th>
                  <th class="hidden md:table-cell">Queue</th>
                  <th class="hidden lg:table-cell">Partition</th>
                  <th class="text-right">Created</th>
                  <th class="text-right">Status</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="message in messages"
                  :key="message.id"
                  class="cursor-pointer"
                  @click="selectMessage(message)"
                >
                  <td>
                    <div class="font-mono text-xs">{{ message.transactionId.substring(0, 20) }}...</div>
                    <div class="text-xs text-gray-500 dark:text-gray-400 md:hidden">
                      {{ message.queue }}
                    </div>
                  </td>
                  <td class="hidden md:table-cell">
                    <div class="text-sm">{{ message.queue }}</div>
                  </td>
                  <td class="hidden lg:table-cell">
                    <span class="text-xs text-gray-600 dark:text-gray-400">{{ message.partition }}</span>
                  </td>
                  <td class="text-right text-xs">{{ formatTime(message.createdAt) }}</td>
                  <td class="text-right">
                    <StatusBadge :status="message.status" :show-dot="false" />
                  </td>
                </tr>
              </tbody>
            </table>
            
            <div v-if="!messages.length && !loading" class="text-center py-12 text-gray-500 text-sm">
              <svg class="w-12 h-12 mx-auto mb-3 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
              </svg>
              <p>No messages found</p>
            </div>
          </div>
          
          <!-- Pagination -->
          <div v-if="totalPages > 1" class="flex items-center justify-between mt-4 pt-4 border-t border-gray-200/30 dark:border-gray-700/30">
            <div class="text-sm text-gray-600 dark:text-gray-400">
              Page {{ currentPage }} of {{ totalPages }}
            </div>
            <div class="flex gap-2">
              <button
                @click="currentPage--"
                :disabled="currentPage === 1"
                class="btn btn-secondary px-3 py-1.5"
              >
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
                </svg>
              </button>
              <button
                @click="currentPage++"
                :disabled="currentPage === totalPages"
                class="btn btn-secondary px-3 py-1.5"
              >
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
                </svg>
              </button>
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
      @action-complete="onActionComplete"
    />
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted } from 'vue';
import { useRoute } from 'vue-router';
import { messagesApi } from '../api/messages';
import { queuesApi } from '../api/queues';
import { formatTime } from '../utils/formatters';

import MessageFilters from '../components/messages/MessageFilters.vue';
import MessageDetailPanel from '../components/messages/MessageDetailPanel.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';

const route = useRoute();

const loading = ref(false);
const error = ref(null);
const messages = ref([]);
const queues = ref([]);
const searchQuery = ref('');
const queueFilter = ref('');
const statusFilter = ref('');
const fromFilter = ref('');
const toFilter = ref('');
const currentPage = ref(1);
const itemsPerPage = 200;
const totalPages = ref(1);
const selectedMessage = ref(null);
const apiNeedsRestart = ref(false);

watch(() => route.query.transactionId, (transactionId) => {
  if (transactionId && messages.value.length) {
    const message = messages.value.find(m => m.transactionId === transactionId);
    if (message) {
      selectedMessage.value = message;
    }
  }
}, { immediate: true });

async function loadData() {
  loading.value = true;
  error.value = null;
  apiNeedsRestart.value = false;
  
  try {
    const queuesRes = await queuesApi.getQueues();
    queues.value = queuesRes.data.queues;
    
    const params = {
      limit: itemsPerPage,
      offset: (currentPage.value - 1) * itemsPerPage,
    };
    
    if (searchQuery.value) {
      try {
        const msgRes = await messagesApi.getMessage(searchQuery.value);
        messages.value = [msgRes.data];
        totalPages.value = 1;
        return;
      } catch {
        // Fall through to regular list
      }
    }
    
    if (queueFilter.value) params.queue = queueFilter.value;
    if (statusFilter.value) params.status = statusFilter.value;
    if (fromFilter.value) params.from = fromFilter.value;
    if (toFilter.value) params.to = toFilter.value;
    
    const messagesRes = await messagesApi.getMessages(params);
    messages.value = messagesRes.data.messages || [];
    
    totalPages.value = messages.value.length === itemsPerPage ? currentPage.value + 1 : currentPage.value;
  } catch (err) {
    error.value = err.response?.data?.error || err.message;
    
    if (error.value.includes('column m.status does not exist')) {
      apiNeedsRestart.value = true;
    }
  } finally {
    loading.value = false;
  }
}

function selectMessage(message) {
  selectedMessage.value = message;
}

async function onActionComplete() {
  selectedMessage.value = null;
  await loadData();
}

watch([searchQuery, queueFilter, statusFilter, fromFilter, toFilter], () => {
  currentPage.value = 1;
  loadData();
});

watch(currentPage, () => {
  loadData();
});

onMounted(() => {
  loadData();
  
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/messages', loadData);
  }
});

onUnmounted(() => {
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/messages', null);
  }
});
</script>

<style scoped>
.page-flat {
  min-height: 100%;
}

.warning-banner {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  border-left: 3px solid rgba(245, 158, 11, 0.5);
}

.dark .warning-banner {
  background: #0a0d14;
  border-left-color: rgba(245, 158, 11, 0.7);
}

.filter-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
}

.dark .filter-flat {
  background: #0a0d14;
}

.filter-flat :deep(.input) {
  background: transparent;
  border: 1px solid rgba(156, 163, 175, 0.15);
  transition: all 0.2s ease;
}

.filter-flat :deep(.input:hover) {
  border-color: rgba(156, 163, 175, 0.25);
}

.filter-flat :deep(.input:focus) {
  background: rgba(244, 63, 94, 0.02);
  border-color: rgba(244, 63, 94, 0.4);
  box-shadow: 0 0 0 3px rgba(244, 63, 94, 0.05);
}

.dark .filter-flat :deep(.input) {
  border-color: rgba(156, 163, 175, 0.1);
}

.dark .filter-flat :deep(.input:focus) {
  background: rgba(244, 63, 94, 0.03);
  border-color: rgba(244, 63, 94, 0.5);
  box-shadow: 0 0 0 3px rgba(244, 63, 94, 0.08);
}

.table-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem 0;
}

.dark .table-flat {
  background: #0a0d14;
}

.table-flat :deep(.table) {
  border-collapse: separate;
  border-spacing: 0;
}

.table-flat :deep(.table thead) {
  background: transparent;
  border-bottom: 1px solid rgba(156, 163, 175, 0.08);
}

.dark .table-flat :deep(.table thead) {
  border-bottom-color: rgba(156, 163, 175, 0.1);
}

.table-flat :deep(.table th) {
  padding: 1rem 1rem;
  font-weight: 600;
  letter-spacing: 0.05em;
}

.table-flat :deep(.table tbody tr) {
  border: none;
  transition: all 0.15s ease;
}

.table-flat :deep(.table tbody tr:nth-child(even)) {
  background: rgba(0, 0, 0, 0.015);
}

.dark .table-flat :deep(.table tbody tr:nth-child(even)) {
  background: rgba(255, 255, 255, 0.02);
}

.table-flat :deep(.table tbody tr:hover) {
  background: rgba(244, 63, 94, 0.03);
  box-shadow: inset 3px 0 0 0 rgba(244, 63, 94, 0.6);
}

.dark .table-flat :deep(.table tbody tr:hover) {
  background: rgba(244, 63, 94, 0.05);
  box-shadow: inset 3px 0 0 0 rgba(244, 63, 94, 0.8);
}

.table-flat :deep(.table td) {
  padding: 0.875rem 1rem;
  border: none;
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
