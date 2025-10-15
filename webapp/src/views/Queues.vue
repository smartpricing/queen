<template>
  <div class="p-4 sm:p-6">
    <div class="space-y-4 sm:space-y-6 max-w-7xl mx-auto">
      <!-- Header with Create Button -->
      <div class="flex items-center justify-end">
        <button @click="showCreateModal = true" class="btn btn-primary">
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4v16m8-8H4" />
          </svg>
          <span class="hidden sm:inline">New Queue</span>
        </button>
      </div>

      <!-- Filters -->
      <QueueFilters
        v-model:search="searchQuery"
        v-model:namespace="namespaceFilter"
        :namespaces="namespaces"
      />

      <LoadingSpinner v-if="loading && !queues.length" />

      <div v-else-if="error" class="card bg-red-50 dark:bg-red-900/20 text-red-600 text-sm">
        <p><strong>Error loading queues:</strong> {{ error }}</p>
      </div>

      <!-- Queues Table -->
      <div v-else class="card">
        <div class="table-container scrollbar-thin">
          <table class="table">
            <thead>
              <tr>
                <th @click="sortBy('name')" class="cursor-pointer hover:text-gray-900 dark:hover:text-gray-100">
                  <div class="flex items-center gap-1">
                    Queue Name
                    <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 16V4m0 0L3 8m4-4l4 4m6 0v12m0 0l4-4m-4 4l-4-4" />
                    </svg>
                  </div>
                </th>
                <th class="hidden md:table-cell">Namespace</th>
                <th class="text-right">Partitions</th>
                <th class="text-right">Pending</th>
                <th class="text-right hidden sm:table-cell">Processing</th>
                <th class="text-right">Total</th>
                <th class="text-right">Actions</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="queue in paginatedQueues"
                :key="queue.id"
                class="cursor-pointer"
                @click="navigateToQueue(queue.name)"
              >
                <td>
                  <div class="font-medium text-gray-900 dark:text-gray-100">{{ queue.name }}</div>
                  <div v-if="queue.namespace" class="text-xs text-gray-500 dark:text-gray-400 md:hidden">
                    {{ queue.namespace }}
                  </div>
                </td>
                <td class="hidden md:table-cell">
                  <span v-if="queue.namespace" class="badge badge-info">{{ queue.namespace }}</span>
                  <span v-else class="text-gray-400">-</span>
                </td>
                <td class="text-right font-medium">{{ queue.partitions }}</td>
                <td class="text-right font-medium">{{ formatNumber(queue.messages?.pending || 0) }}</td>
                <td class="text-right font-medium hidden sm:table-cell">{{ formatNumber(queue.messages?.processing || 0) }}</td>
                <td class="text-right font-medium">{{ formatNumber(queue.messages?.total || 0) }}</td>
                <td class="text-right" @click.stop>
                  <button
                    @click="confirmDelete(queue)"
                    class="p-1.5 rounded hover:bg-red-50 dark:hover:bg-red-900/20 text-red-600 dark:text-red-400 transition-colors"
                    title="Delete queue"
                  >
                    <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                    </svg>
                  </button>
                </td>
              </tr>
            </tbody>
          </table>
          
          <div v-if="!filteredQueues.length" class="text-center py-12 text-gray-500 text-sm">
            <svg class="w-12 h-12 mx-auto mb-3 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
            </svg>
            <p>No queues found</p>
            <button @click="showCreateModal = true" class="btn btn-primary mt-3">
              Create your first queue
            </button>
          </div>
        </div>
        
        <!-- Pagination -->
        <div v-if="totalPages > 1" class="flex items-center justify-between mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
          <div class="text-sm text-gray-600 dark:text-gray-400">
            Showing {{ startIndex + 1 }}-{{ endIndex }} of {{ filteredQueues.length }}
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
            <span class="px-3 py-1.5 text-sm">{{ currentPage }} / {{ totalPages }}</span>
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

      <!-- Create Queue Modal -->
      <CreateQueueModal
        :is-open="showCreateModal"
        @close="showCreateModal = false"
        @created="onQueueCreated"
      />

      <!-- Delete Confirmation Modal -->
      <ConfirmDialog
        :is-open="showDeleteConfirm"
        title="Delete Queue"
        :message="`Are you sure you want to delete '${queueToDelete?.name}'? This action cannot be undone.`"
        confirm-text="Delete"
        confirm-class="btn-danger"
        @confirm="deleteQueue"
        @cancel="showDeleteConfirm = false"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import { queuesApi } from '../api/queues';
import { resourcesApi } from '../api/resources';
import { formatNumber } from '../utils/formatters';

import QueueFilters from '../components/queues/QueueFilters.vue';
import CreateQueueModal from '../components/queues/CreateQueueModal.vue';
import ConfirmDialog from '../components/common/ConfirmDialog.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';

const router = useRouter();

const loading = ref(false);
const error = ref(null);
const queues = ref([]);
const namespaces = ref([]);
const searchQuery = ref('');
const namespaceFilter = ref('');
const sortColumn = ref('name');
const sortDirection = ref('asc');
const currentPage = ref(1);
const itemsPerPage = 20;

const showCreateModal = ref(false);
const showDeleteConfirm = ref(false);
const queueToDelete = ref(null);

const filteredQueues = computed(() => {
  let filtered = queues.value;
  
  // Filter by search
  if (searchQuery.value) {
    const search = searchQuery.value.toLowerCase();
    filtered = filtered.filter(q => 
      q.name.toLowerCase().includes(search) ||
      q.namespace?.toLowerCase().includes(search)
    );
  }
  
  // Filter by namespace
  if (namespaceFilter.value) {
    filtered = filtered.filter(q => q.namespace === namespaceFilter.value);
  }
  
  // Sort
  filtered = [...filtered].sort((a, b) => {
    let aVal = a[sortColumn.value];
    let bVal = b[sortColumn.value];
    
    if (sortColumn.value === 'name') {
      aVal = a.name.toLowerCase();
      bVal = b.name.toLowerCase();
    }
    
    if (aVal < bVal) return sortDirection.value === 'asc' ? -1 : 1;
    if (aVal > bVal) return sortDirection.value === 'asc' ? 1 : -1;
    return 0;
  });
  
  return filtered;
});

const totalPages = computed(() => Math.ceil(filteredQueues.value.length / itemsPerPage));
const startIndex = computed(() => (currentPage.value - 1) * itemsPerPage);
const endIndex = computed(() => Math.min(startIndex.value + itemsPerPage, filteredQueues.value.length));
const paginatedQueues = computed(() => filteredQueues.value.slice(startIndex.value, endIndex.value));

function sortBy(column) {
  if (sortColumn.value === column) {
    sortDirection.value = sortDirection.value === 'asc' ? 'desc' : 'asc';
  } else {
    sortColumn.value = column;
    sortDirection.value = 'asc';
  }
}

function navigateToQueue(queueName) {
  router.push(`/queues/${queueName}`);
}

function confirmDelete(queue) {
  queueToDelete.value = queue;
  showDeleteConfirm.value = true;
}

async function deleteQueue() {
  if (!queueToDelete.value) return;
  
  try {
    await queuesApi.deleteQueue(queueToDelete.value.name);
    showDeleteConfirm.value = false;
    queueToDelete.value = null;
    await loadData();
  } catch (err) {
    error.value = err.message;
  }
}

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    const [queuesRes, namespacesRes] = await Promise.all([
      queuesApi.getQueues(),
      resourcesApi.getNamespaces(),
    ]);
    
    queues.value = queuesRes.data.queues;
    namespaces.value = namespacesRes.data.namespaces;
  } catch (err) {
    error.value = err.message;
    console.error('Queues error:', err);
  } finally {
    loading.value = false;
  }
}

async function onQueueCreated() {
  await loadData();
}

onMounted(() => {
  loadData();
  
  // Register refresh callback for header button
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/queues', loadData);
  }
});

onUnmounted(() => {
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/queues', null);
  }
});
</script>
