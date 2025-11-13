<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <!-- Filters with New Queue Button -->
        <div class="filter-card">
          <div class="flex flex-col sm:flex-row gap-3">
            <div class="flex-1">
              <input
                v-model="searchQuery"
                type="text"
                placeholder="Search queues..."
                class="input"
              />
            </div>
            
            <div class="sm:w-48">
              <CustomSelect
                v-model="namespaceFilter"
                :options="namespaceOptions"
                placeholder="All Namespaces"
                :clearable="true"
                :searchable="true"
              />
            </div>
            
            <button
              v-if="searchQuery || namespaceFilter"
              @click="clearFilters"
              class="btn btn-secondary whitespace-nowrap"
            >
              Clear Filters
            </button>
            
            <button @click="showCreateModal = true" class="btn btn-primary whitespace-nowrap">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4v16m8-8H4" />
              </svg>
              <span class="hidden sm:inline">New Queue</span>
            </button>
          </div>
        </div>

        <LoadingSpinner v-if="loading && !queues.length" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading queues:</strong> {{ error }}</p>
        </div>

        <!-- Queues Table -->
        <div v-else class="chart-card">
          <div class="chart-body">
            <div class="table-container scrollbar-thin">
              <table class="table">
              <thead>
                <tr>
                  <th @click="sortBy('name')" class="cursor-pointer hover:text-emerald-600 dark:hover:text-emerald-400 transition-colors">
                    <div class="flex items-center gap-1">
                      Queue Name
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('name')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
                  <th @click="sortBy('namespace')" class="hidden md:table-cell cursor-pointer hover:text-emerald-600 dark:hover:text-emerald-400 transition-colors">
                    <div class="flex items-center gap-1">
                      Namespace
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('namespace')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
                  <th @click="sortBy('partitions')" class="text-right cursor-pointer hover:text-emerald-600 dark:hover:text-emerald-400 transition-colors">
                    <div class="flex items-center justify-end gap-1">
                      Partitions
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('partitions')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
                  <th @click="sortBy('pending')" class="text-right cursor-pointer hover:text-emerald-600 dark:hover:text-emerald-400 transition-colors">
                    <div class="flex items-center justify-end gap-1">
                      Pending
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('pending')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
                  <th @click="sortBy('processing')" class="text-right hidden sm:table-cell cursor-pointer hover:text-emerald-600 dark:hover:text-emerald-400 transition-colors">
                    <div class="flex items-center justify-end gap-1">
                      Processing
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('processing')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
                  <th @click="sortBy('total')" class="text-right cursor-pointer hover:text-emerald-600 dark:hover:text-emerald-400 transition-colors">
                    <div class="flex items-center justify-end gap-1">
                      Total
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('total')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
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
            <div v-if="totalPages > 1" class="flex items-center justify-between mt-4 pt-4 border-t border-gray-200/60 dark:border-gray-800/60">
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
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import { queuesApi } from '../api/queues';
import { resourcesApi } from '../api/resources';
import { formatNumber } from '../utils/formatters';

import CustomSelect from '../components/common/CustomSelect.vue';
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

const namespaceOptions = computed(() => [
  { value: '', label: 'All Namespaces' },
  ...(namespaces.value || []).map(ns => ({ 
    value: ns.namespace, 
    label: `${ns.namespace} (${ns.queues})` 
  }))
]);

const filteredQueues = computed(() => {
  let filtered = queues.value;
  
  if (searchQuery.value) {
    const search = searchQuery.value.toLowerCase();
    filtered = filtered.filter(q => 
      q.name.toLowerCase().includes(search) ||
      q.namespace?.toLowerCase().includes(search)
    );
  }
  
  if (namespaceFilter.value) {
    filtered = filtered.filter(q => q.namespace === namespaceFilter.value);
  }
  
  filtered = [...filtered].sort((a, b) => {
    let aVal, bVal;
    
    switch (sortColumn.value) {
      case 'name':
        aVal = a.name.toLowerCase();
        bVal = b.name.toLowerCase();
        break;
      case 'namespace':
        aVal = a.namespace?.toLowerCase() || '';
        bVal = b.namespace?.toLowerCase() || '';
        break;
      case 'partitions':
        aVal = a.partitions || 0;
        bVal = b.partitions || 0;
        break;
      case 'pending':
        aVal = a.messages?.pending || 0;
        bVal = b.messages?.pending || 0;
        break;
      case 'processing':
        aVal = a.messages?.processing || 0;
        bVal = b.messages?.processing || 0;
        break;
      case 'total':
        aVal = a.messages?.total || 0;
        bVal = b.messages?.total || 0;
        break;
      default:
        return 0;
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

function getSortClass(column) {
  if (sortColumn.value !== column) {
    return 'opacity-30';
  }
  return sortDirection.value === 'asc' ? '' : 'rotate-180';
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

function clearFilters() {
  searchQuery.value = '';
  namespaceFilter.value = '';
}

onMounted(() => {
  loadData();
  
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

<style scoped>
/* Professional Page Design */
.page-professional {
  @apply min-h-screen bg-gray-50 dark:bg-[#0d1117];
  background-image: 
    radial-gradient(at 0% 0%, rgba(5, 150, 105, 0.03) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.03) 0px, transparent 50%);
}

.dark .page-professional {
  background-image: 
    radial-gradient(at 0% 0%, rgba(5, 150, 105, 0.05) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.05) 0px, transparent 50%);
}

.page-content {
  @apply px-6 lg:px-8 py-6;
}

.page-inner {
  @apply space-y-6;
}

/* All other styles are inherited from professional.css */
</style>
