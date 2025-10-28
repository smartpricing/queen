<template>
  <div class="page-professional">
    <div class="page-content">
      <div class="page-inner">
        <!-- Info Card -->
        <div class="info-card">
          <div class="flex gap-3">
            <svg class="w-5 h-5 flex-shrink-0 mt-0.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <div class="text-sm">
              <p class="font-semibold mb-1 text-gray-900 dark:text-white">Consumer Groups Overview</p>
              <p class="text-gray-700 dark:text-gray-300">
                Consumer groups enable multiple consumers to process messages in parallel without duplication. 
                Each group maintains its own consumption cursor per partition.
              </p>
            </div>
          </div>
        </div>

        <!-- Filters -->
        <div class="filter-card">
          <div class="flex flex-col sm:flex-row gap-3">
            <div class="flex-1">
              <input
                v-model="searchQuery"
                type="text"
                placeholder="Search consumer groups..."
                class="input"
              />
            </div>
            
            <select v-model="statusFilter" class="input sm:w-48">
              <option value="">All States</option>
              <option value="Stable">Stable</option>
              <option value="Lagging">Lagging</option>
              <option value="Dead">Dead</option>
            </select>
            
            <button
              v-if="searchQuery || statusFilter"
              @click="clearFilters"
              class="btn btn-secondary whitespace-nowrap"
            >
              Clear Filters
            </button>
          </div>
        </div>

        <LoadingSpinner v-if="loading && !consumerGroups.length" />

        <div v-else-if="error" class="error-card">
          <p><strong>Error loading consumer groups:</strong> {{ error }}</p>
        </div>

        <!-- Consumer Groups Stats -->
        <div class="metrics-grid">
          <div class="metric-card-compact">
            <span class="metric-label-sm">ACTIVE GROUPS</span>
            <div class="metric-value-sm text-emerald-600 dark:text-emerald-400">{{ stats.activeGroups }}</div>
          </div>
          <div class="metric-card-compact">
            <span class="metric-label-sm">TOTAL CONSUMERS</span>
            <div class="metric-value-sm text-blue-600 dark:text-blue-400">{{ stats.totalConsumers }}</div>
          </div>
          <div class="metric-card-compact">
            <span class="metric-label-sm">TOPICS MONITORED</span>
            <div class="metric-value-sm text-indigo-600 dark:text-indigo-400">{{ stats.totalTopics }}</div>
          </div>
          <div class="metric-card-compact">
            <span class="metric-label-sm">AVG LAG</span>
            <div class="metric-value-sm text-orange-600 dark:text-orange-400">{{ stats.avgLag }}</div>
          </div>
        </div>

        <!-- Consumer Groups Table -->
        <div class="data-card">
          <div class="table-container scrollbar-thin">
            <table class="table">
              <thead>
                <tr>
                  <th>Consumer Group</th>
                  <th class="hidden md:table-cell">Topics</th>
                  <th class="text-right">Members</th>
                  <th class="text-right hidden lg:table-cell">Offset Lag</th>
                  <th class="text-right">Time Lag</th>
                  <th class="text-right">State</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="group in filteredGroups" 
                  :key="group.name"
                  @click="selectGroup(group)"
                  class="cursor-pointer"
                >
                  <td>
                    <div class="font-medium text-gray-900 dark:text-gray-100">{{ group.name }}</div>
                    <div class="text-xs text-gray-500 dark:text-gray-400 md:hidden">
                      {{ group.topics.length }} topic{{ group.topics.length !== 1 ? 's' : '' }}
                    </div>
                  </td>
                  <td class="hidden md:table-cell">
                    <div class="flex flex-wrap gap-1">
                      <span
                        v-for="topic in group.topics.slice(0, 2)"
                        :key="topic"
                        class="badge badge-info text-xs"
                      >
                        {{ topic }}
                      </span>
                      <span v-if="group.topics.length > 2" class="text-xs text-gray-500">
                        +{{ group.topics.length - 2 }} more
                      </span>
                    </div>
                  </td>
                  <td class="text-right font-medium">{{ group.members }}</td>
                  <td class="text-right font-medium hidden lg:table-cell">
                    {{ formatNumber(group.totalLag) }}
                  </td>
                  <td class="text-right font-medium">
                    {{ group.maxTimeLag > 0 ? formatDuration(group.maxTimeLag) : '0ms' }}
                  </td>
                  <td class="text-right">
                    <StatusBadge :status="group.state" />
                  </td>
                </tr>
              </tbody>
            </table>
            
            <div v-if="!filteredGroups.length && !loading" class="text-center py-12 text-gray-500 text-sm">
              <svg class="w-12 h-12 mx-auto mb-3 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
              </svg>
              <p>No consumer groups found</p>
              <p class="text-xs mt-2">Consumer groups are created automatically when consumers connect with a consumerGroup parameter.</p>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Consumer Group Detail Modal -->
    <div v-if="selectedGroup" class="modal-overlay" @click="closeDetail">
      <div class="modal-content" @click.stop>
        <div class="modal-header">
          <div>
            <h2 class="text-2xl font-bold text-gray-900 dark:text-gray-100">
              {{ selectedGroup.name }}
            </h2>
            <p class="text-sm text-gray-500 dark:text-gray-400 mt-1">
              Consumer Group Details
            </p>
          </div>
          <button @click="closeDetail" class="close-btn">
            <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>

        <div class="modal-body">
          <!-- Summary Stats -->
          <div class="grid grid-cols-2 lg:grid-cols-4 gap-3 mb-6">
            <div class="stat-card">
              <div class="stat-label">Total Members</div>
              <div class="stat-value">{{ selectedGroup.members }}</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">Topics</div>
              <div class="stat-value">{{ selectedGroup.topics.length }}</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">Total Lag</div>
              <div class="stat-value">{{ formatNumber(selectedGroup.totalLag) }}</div>
            </div>
            <div class="stat-card">
              <div class="stat-label">Max Time Lag</div>
              <div class="stat-value">{{ selectedGroup.maxTimeLag > 0 ? formatDuration(selectedGroup.maxTimeLag) : '0ms' }}</div>
            </div>
          </div>

          <!-- Per-Queue Details -->
          <div v-for="(queueData, queueName) in selectedGroup.queues" :key="queueName" class="queue-section">
            <h3 class="queue-title">
              <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10" />
              </svg>
              {{ queueName }}
              <span class="partition-count">{{ queueData.partitions.length }} partition{{ queueData.partitions.length !== 1 ? 's' : '' }}</span>
            </h3>

            <div class="partitions-table">
              <table class="detail-table">
                <thead>
                  <tr>
                    <th>Partition</th>
                    <th>Worker ID</th>
                    <th class="text-right">Consumed</th>
                    <th class="text-right">Offset Lag</th>
                    <th class="text-right">Time Lag</th>
                    <th class="text-center">Lease</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="partition in queueData.partitions" :key="partition.partition">
                    <td class="font-medium">{{ partition.partition }}</td>
                    <td>
                      <code class="worker-id">{{ partition.workerId || 'N/A' }}</code>
                    </td>
                    <td class="text-right">{{ formatNumber(partition.totalConsumed) }}</td>
                    <td class="text-right font-medium" :class="partition.offsetLag > 0 ? 'text-orange-600 dark:text-orange-400' : 'text-green-600 dark:text-green-400'">
                      {{ formatNumber(partition.offsetLag) }}
                    </td>
                    <td class="text-right font-medium" :class="partition.timeLagSeconds > 300 ? 'text-red-600 dark:text-red-400' : 'text-gray-900 dark:text-gray-100'">
                      {{ partition.timeLagSeconds > 0 ? formatDuration(partition.timeLagSeconds) : '0ms' }}
                    </td>
                    <td class="text-center">
                      <span v-if="partition.leaseActive" class="badge badge-success text-xs">Active</span>
                      <span v-else class="badge badge-secondary text-xs">Inactive</span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { consumersApi } from '../api/consumers';
import { formatNumber, formatDuration } from '../utils/formatters';

import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';

const loading = ref(false);
const error = ref(null);
const consumerGroups = ref([]);
const searchQuery = ref('');
const statusFilter = ref('');
const selectedGroup = ref(null);

const filteredGroups = computed(() => {
  let filtered = consumerGroups.value;
  
  if (searchQuery.value) {
    const search = searchQuery.value.toLowerCase();
    filtered = filtered.filter(g => 
      g.name.toLowerCase().includes(search) ||
      g.topics.some(t => t.toLowerCase().includes(search))
    );
  }
  
  if (statusFilter.value) {
    filtered = filtered.filter(g => g.state === statusFilter.value);
  }
  
  return filtered;
});

const stats = computed(() => {
  const groups = consumerGroups.value;
  
  return {
    activeGroups: groups.filter(g => g.state === 'Stable').length,
    totalConsumers: groups.reduce((sum, g) => sum + g.members, 0),
    totalTopics: new Set(groups.flatMap(g => g.topics)).size,
    avgLag: groups.length > 0 
      ? Math.round(groups.reduce((sum, g) => sum + g.totalLag, 0) / groups.length)
      : 0,
  };
});

function clearFilters() {
  searchQuery.value = '';
  statusFilter.value = '';
}

function selectGroup(group) {
  selectedGroup.value = group;
}

function closeDetail() {
  selectedGroup.value = null;
}

async function loadData() {
  loading.value = true;
  error.value = null;
  
  try {
    const groups = await consumersApi.getConsumerGroups();
    consumerGroups.value = groups;
  } catch (err) {
    error.value = err.message;
    console.error('Consumer groups error:', err);
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadData();
  
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/consumer-groups', loadData);
  }
});

onUnmounted(() => {
  if (window.registerRefreshCallback) {
    window.registerRefreshCallback('/consumer-groups', null);
  }
});
</script>

<style scoped>
.page-professional {
  @apply min-h-screen bg-gray-50 dark:bg-[#0d1117];
  background-image: 
    radial-gradient(at 0% 0%, rgba(59, 130, 246, 0.03) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.03) 0px, transparent 50%);
}

.dark .page-professional {
  background-image: 
    radial-gradient(at 0% 0%, rgba(59, 130, 246, 0.05) 0px, transparent 50%),
    radial-gradient(at 100% 0%, rgba(99, 102, 241, 0.05) 0px, transparent 50%);
}

.page-content {
  @apply px-6 lg:px-8 py-6;
}

.page-inner {
  @apply space-y-6;
}

.info-card {
  @apply bg-blue-50/50 dark:bg-blue-900/10 border border-blue-200/60 dark:border-blue-800/40;
  @apply rounded-xl p-4;
  box-shadow: 0 1px 3px 0 rgba(59, 130, 246, 0.1);
}

.filter-card {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl p-4;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
}

.dark .filter-card {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}

.filter-card :deep(.input) {
  @apply bg-gray-50 dark:bg-gray-800/40 border border-gray-200/60 dark:border-gray-700/60;
  @apply rounded-lg px-3 py-2 text-sm;
  transition: all 0.15s cubic-bezier(0.4, 0, 0.2, 1);
}

.filter-card :deep(.input:hover) {
  @apply border-gray-300 dark:border-gray-600;
}

.filter-card :deep(.input:focus) {
  @apply bg-white dark:bg-gray-800 border-blue-500 dark:border-blue-400;
  box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1);
  outline: none;
}

.metrics-grid {
  @apply grid grid-cols-2 lg:grid-cols-4 gap-4;
}

.metric-card-compact {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-lg p-3;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
}

.dark .metric-card-compact {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}

.metric-label-sm {
  @apply text-[10px] font-bold text-gray-500 dark:text-gray-400 tracking-wider uppercase block mb-1;
  letter-spacing: 0.05em;
}

.metric-value-sm {
  @apply text-xl font-bold tracking-tight;
  letter-spacing: -0.02em;
}

.data-card {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl overflow-hidden;
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.04), 0 1px 2px 0 rgba(0, 0, 0, 0.02);
}

.dark .data-card {
  box-shadow: 0 1px 3px 0 rgba(0, 0, 0, 0.4), 0 1px 2px 0 rgba(0, 0, 0, 0.2);
}

.data-card :deep(.table) {
  @apply border-0;
}

.data-card :deep(.table thead) {
  @apply bg-transparent border-b border-gray-200/80 dark:border-gray-800/80;
  background: linear-gradient(to bottom, rgba(249, 250, 251, 0.5), transparent);
}

.dark .data-card :deep(.table thead) {
  background: linear-gradient(to bottom, rgba(255, 255, 255, 0.01), transparent);
}

.data-card :deep(.table thead th) {
  @apply text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider;
  @apply py-3 px-5;
  letter-spacing: 0.05em;
}

.data-card :deep(.table tbody tr) {
  @apply border-b border-gray-100/60 dark:border-gray-800/40;
  transition: background-color 0.15s cubic-bezier(0.4, 0, 0.2, 1);
}

.data-card :deep(.table tbody tr:last-child) {
  @apply border-b-0;
}

.data-card :deep(.table tbody tr:hover) {
  @apply bg-blue-50/50 dark:bg-blue-900/10;
}

.data-card :deep(.table tbody td) {
  @apply py-3.5 px-5 text-sm;
}

.error-card {
  @apply bg-red-50 dark:bg-red-900/10 border border-red-200/60 dark:border-red-800/60;
  @apply rounded-xl p-4 text-sm text-red-800 dark:text-red-400;
  box-shadow: 0 1px 3px 0 rgba(239, 68, 68, 0.1);
}

/* Modal styles */
.modal-overlay {
  @apply fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center z-50 p-4;
}

.modal-content {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-xl max-w-5xl w-full max-h-[90vh] flex flex-col;
  box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.3), 0 10px 10px -5px rgba(0, 0, 0, 0.2);
}

.modal-header {
  @apply flex justify-between items-start p-5 border-b border-gray-200/80 dark:border-gray-800/80;
}

.close-btn {
  @apply p-2 rounded-lg text-gray-500 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-800;
  transition: all 0.15s cubic-bezier(0.4, 0, 0.2, 1);
}

.modal-body {
  @apply p-5 overflow-y-auto flex-1;
}

.stat-card {
  @apply bg-blue-50/50 dark:bg-blue-900/10 border border-blue-200/40 dark:border-blue-800/30;
  @apply rounded-lg p-4;
}

.stat-label {
  @apply text-xs font-semibold uppercase tracking-wider text-gray-500 dark:text-gray-400 mb-1;
  letter-spacing: 0.05em;
}

.stat-value {
  @apply text-xl font-bold text-blue-600 dark:text-blue-400 tracking-tight;
}

.queue-section {
  @apply mb-4;
}

.queue-title {
  @apply flex items-center gap-2 text-base font-semibold text-gray-900 dark:text-white mb-3;
  @apply p-3 bg-gray-50/80 dark:bg-gray-800/30 rounded-lg border-l-2 border-blue-500;
}

.partition-count {
  @apply text-xs font-medium text-gray-500 dark:text-gray-400 ml-auto;
}

.partitions-table {
  @apply rounded-lg overflow-hidden border border-gray-200/40 dark:border-gray-800/40;
}

.detail-table {
  @apply w-full text-sm border-collapse;
}

.detail-table thead {
  @apply bg-gray-50 dark:bg-gray-800/30 border-b border-gray-200/80 dark:border-gray-800/80;
}

.detail-table th {
  @apply py-2.5 px-4 font-semibold text-left text-xs uppercase tracking-wider text-gray-500 dark:text-gray-400;
  letter-spacing: 0.05em;
}

.detail-table td {
  @apply py-2.5 px-4 border-b border-gray-100/60 dark:border-gray-800/40 text-gray-900 dark:text-gray-100;
}

.detail-table tbody tr:last-child td {
  @apply border-b-0;
}

.detail-table tbody tr:hover {
  @apply bg-gray-50/60 dark:bg-gray-800/20;
}

.worker-id {
  @apply bg-indigo-100 dark:bg-indigo-900/30 text-indigo-700 dark:text-indigo-300;
  @apply px-2 py-1 rounded text-xs font-mono;
}
</style>
