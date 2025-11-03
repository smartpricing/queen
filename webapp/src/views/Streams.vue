<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <!-- Filters with New Stream Button -->
        <div class="filter-card">
          <div class="flex flex-col sm:flex-row gap-3">
            <div class="flex-1">
              <input
                v-model="searchQuery"
                type="text"
                placeholder="Search streams..."
                class="input"
              />
            </div>
            
            <select v-model="namespaceFilter" class="input sm:w-48">
              <option value="">All Namespaces</option>
              <option v-for="ns in namespaces" :key="ns" :value="ns">
                {{ ns }}
              </option>
            </select>
            
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
              <span class="hidden sm:inline">New Stream</span>
            </button>
          </div>
        </div>

        <LoadingSpinner v-if="loading && !streams.length" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading streams:</strong> {{ error }}</p>
        </div>

        <!-- Streams Table -->
        <div v-else class="chart-card">
          <div class="chart-body">
            <div class="table-container scrollbar-thin">
              <table class="table">
              <thead>
                <tr>
                  <th @click="sortBy('name')" class="cursor-pointer hover:text-purple-600 dark:hover:text-purple-400 transition-colors">
                    <div class="flex items-center gap-1">
                      Stream Name
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('name')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
                  <th @click="sortBy('namespace')" class="hidden md:table-cell cursor-pointer hover:text-purple-600 dark:hover:text-purple-400 transition-colors">
                    <div class="flex items-center gap-1">
                      Namespace
                      <svg class="w-3 h-3 transition-transform" :class="getSortClass('namespace')" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
                        <path stroke-linecap="round" stroke-linejoin="round" d="M19 9l-7 7-7-7" />
                      </svg>
                    </div>
                  </th>
                  <th class="hidden lg:table-cell">Type</th>
                  <th class="text-right">Window Duration</th>
                  <th class="text-right hidden sm:table-cell">Active Leases</th>
                  <th class="text-right hidden sm:table-cell">Consumer Groups</th>
                  <th class="text-right">Actions</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="stream in paginatedStreams"
                  :key="stream.id"
                  class="cursor-pointer hover:bg-gray-50 dark:hover:bg-gray-800/30"
                  @click="viewStreamDetails(stream)"
                >
                  <td>
                    <div class="font-medium text-gray-900 dark:text-gray-100">{{ stream.name }}</div>
                    <div class="text-xs text-gray-500 dark:text-gray-400 mt-0.5">
                      {{ stream.sourceQueues?.length || 0 }} source queue{{ stream.sourceQueues?.length !== 1 ? 's' : '' }}
                    </div>
                  </td>
                  <td class="hidden md:table-cell">
                    <span v-if="stream.namespace" class="badge badge-info">{{ stream.namespace }}</span>
                    <span v-else class="text-gray-400">-</span>
                  </td>
                  <td class="hidden lg:table-cell">
                    <span class="badge badge-purple">
                      {{ stream.partitioned ? 'Partitioned' : 'Global' }}
                    </span>
                  </td>
                  <td class="text-right font-medium">
                    {{ formatDuration(stream.windowDurationMs) }}
                  </td>
                  <td class="text-right font-medium hidden sm:table-cell text-purple-600 dark:text-purple-400">
                    {{ formatNumber(stream.activeLeases || 0) }}
                  </td>
                  <td class="text-right font-medium hidden sm:table-cell">
                    {{ formatNumber(stream.consumerGroups || 0) }}
                  </td>
                  <td class="text-right" @click.stop>
                    <button
                      @click="confirmDelete(stream)"
                      class="p-1.5 rounded hover:bg-red-50 dark:hover:bg-red-900/20 text-red-600 dark:text-red-400 transition-colors"
                      title="Delete stream"
                    >
                      <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                      </svg>
                    </button>
                  </td>
                </tr>
              </tbody>
            </table>
            
            <div v-if="!filteredStreams.length" class="text-center py-12 text-gray-500 text-sm">
              <svg class="w-12 h-12 mx-auto mb-3 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M15 13l-3-3m0 0l-3 3m3-3v12" />
              </svg>
              <p>No streams found</p>
              <button @click="showCreateModal = true" class="btn btn-primary mt-3">
                Create your first stream
              </button>
            </div>
            </div>
            
            <!-- Pagination -->
            <div v-if="totalPages > 1" class="pagination">
              <button
                @click="currentPage--"
                :disabled="currentPage === 1"
                class="pagination-btn"
              >
                Previous
              </button>
              
              <span class="pagination-info">
                Page {{ currentPage }} of {{ totalPages }}
              </span>
              
              <button
                @click="currentPage++"
                :disabled="currentPage === totalPages"
                class="pagination-btn"
              >
                Next
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
    
    <!-- Stream Detail Modal -->
    <StreamDetailModal
      v-if="selectedStream"
      :stream="selectedStream"
      @close="selectedStream = null"
      @delete="handleDeleteFromModal"
    />
    
    <!-- Create Stream Modal -->
    <CreateStreamModal
      v-if="showCreateModal"
      @close="showCreateModal = false"
      @created="handleStreamCreated"
    />
    
    <!-- Delete Confirmation -->
    <ConfirmDialog
      v-if="streamToDelete"
      title="Delete Stream"
      :message="`Are you sure you want to delete stream '${streamToDelete.name}'? This action cannot be undone.`"
      confirmText="Delete"
      confirmClass="btn-danger"
      @confirm="deleteStream"
      @cancel="streamToDelete = null"
    />
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { streamsApi } from '../api/streams';
import { formatNumber } from '../utils/formatters';

import LoadingSpinner from '../components/common/LoadingSpinner.vue';
import ConfirmDialog from '../components/common/ConfirmDialog.vue';
import CreateStreamModal from '../components/streams/CreateStreamModal.vue';
import StreamDetailModal from '../components/streams/StreamDetailModal.vue';

const loading = ref(false);
const error = ref(null);
const streams = ref([]);
const searchQuery = ref('');
const namespaceFilter = ref('');
const currentPage = ref(1);
const pageSize = 20;
const sortKey = ref('name');
const sortOrder = ref('asc');
const showCreateModal = ref(false);
const streamToDelete = ref(null);
const selectedStream = ref(null);

// Get unique namespaces
const namespaces = computed(() => {
  const ns = new Set();
  streams.value.forEach(stream => {
    if (stream.namespace) ns.add(stream.namespace);
  });
  return Array.from(ns).sort();
});

// Filter streams
const filteredStreams = computed(() => {
  let result = streams.value;
  
  // Apply search filter
  if (searchQuery.value) {
    const query = searchQuery.value.toLowerCase();
    result = result.filter(stream =>
      stream.name.toLowerCase().includes(query) ||
      stream.namespace?.toLowerCase().includes(query)
    );
  }
  
  // Apply namespace filter
  if (namespaceFilter.value) {
    result = result.filter(stream => stream.namespace === namespaceFilter.value);
  }
  
  // Apply sorting
  result = [...result].sort((a, b) => {
    let aVal, bVal;
    
    switch (sortKey.value) {
      case 'name':
        aVal = a.name.toLowerCase();
        bVal = b.name.toLowerCase();
        break;
      case 'namespace':
        aVal = a.namespace?.toLowerCase() || '';
        bVal = b.namespace?.toLowerCase() || '';
        break;
      default:
        aVal = a[sortKey.value] || 0;
        bVal = b[sortKey.value] || 0;
    }
    
    if (aVal < bVal) return sortOrder.value === 'asc' ? -1 : 1;
    if (aVal > bVal) return sortOrder.value === 'asc' ? 1 : -1;
    return 0;
  });
  
  return result;
});

// Pagination
const totalPages = computed(() => Math.ceil(filteredStreams.value.length / pageSize));

const paginatedStreams = computed(() => {
  const start = (currentPage.value - 1) * pageSize;
  const end = start + pageSize;
  return filteredStreams.value.slice(start, end);
});

function sortBy(key) {
  if (sortKey.value === key) {
    sortOrder.value = sortOrder.value === 'asc' ? 'desc' : 'asc';
  } else {
    sortKey.value = key;
    sortOrder.value = 'asc';
  }
}

function getSortClass(key) {
  if (sortKey.value !== key) return 'opacity-30';
  return sortOrder.value === 'asc' ? '' : 'rotate-180';
}

function clearFilters() {
  searchQuery.value = '';
  namespaceFilter.value = '';
  currentPage.value = 1;
}

function formatDuration(ms) {
  if (!ms) return '-';
  
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  const days = Math.floor(hours / 24);
  
  if (days > 0) return `${days}d`;
  if (hours > 0) return `${hours}h`;
  if (minutes > 0) return `${minutes}m`;
  return `${seconds}s`;
}

function viewStreamDetails(stream) {
  selectedStream.value = stream;
}

function confirmDelete(stream) {
  streamToDelete.value = stream;
}

async function deleteStream() {
  if (!streamToDelete.value) return;
  
  try {
    await streamsApi.deleteStream(streamToDelete.value.name);
    streams.value = streams.value.filter(s => s.id !== streamToDelete.value.id);
    streamToDelete.value = null;
  } catch (err) {
    alert(`Failed to delete stream: ${err.message}`);
  }
}

function handleStreamCreated() {
  showCreateModal.value = false;
  loadStreams();
}

function handleDeleteFromModal(stream) {
  selectedStream.value = null;
  streamToDelete.value = stream;
}

async function loadStreams() {
  loading.value = true;
  error.value = null;
  
  try {
    const response = await streamsApi.getStreams();
    streams.value = response.data.streams || [];
  } catch (err) {
    error.value = err.message;
    console.error('Streams error:', err);
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadStreams();
  
  // Register refresh callback for header button
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/streams', loadStreams);
  }
});

onUnmounted(() => {
  // Clean up callback
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/streams', null);
  }
});
</script>

<style scoped>
@import '../assets/styles/professional.css';

.badge-purple {
  @apply bg-purple-100 text-purple-700 dark:bg-purple-900/30 dark:text-purple-400;
  @apply px-2 py-0.5 rounded text-xs font-medium;
}
</style>

