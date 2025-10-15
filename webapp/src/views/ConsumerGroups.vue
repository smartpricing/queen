<template>
  <div class="page-flat">
    <div class="p-4">
      <div class="space-y-3 sm:space-y-4">
        <!-- Info Card -->
        <div class="info-banner">
          <div class="flex gap-3">
            <svg class="w-5 h-5 flex-shrink-0 mt-0.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <div class="text-sm">
              <p class="font-semibold mb-1">Consumer Groups Overview</p>
              <p class="text-purple-800 dark:text-purple-200">
                Consumer groups enable multiple consumers to process messages in parallel without duplication. 
                Each group maintains its own consumption cursor per partition.
              </p>
            </div>
          </div>
        </div>

        <!-- Filters -->
        <div class="filter-flat">
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

        <div v-else-if="error" class="error-flat">
          <p><strong>Error loading consumer groups:</strong> {{ error }}</p>
        </div>

        <!-- Consumer Groups Stats -->
        <div class="grid grid-cols-2 lg:grid-cols-4 gap-2 sm:gap-3">
          <div class="metric-flat">
            <div class="flex items-start gap-3">
              <div class="metric-icon-flat bg-green-500/10 dark:bg-green-500/20">
                <svg class="w-6 h-6 text-green-600 dark:text-green-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
              </div>
              <div class="flex-1">
                <p class="metric-label">Active Groups</p>
                <p class="metric-value-flat">{{ stats.activeGroups }}</p>
              </div>
            </div>
          </div>
          <div class="metric-flat">
            <div class="flex items-start gap-3">
              <div class="metric-icon-flat bg-purple-500/10 dark:bg-purple-500/20">
                <svg class="w-6 h-6 text-purple-600 dark:text-purple-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
                </svg>
              </div>
              <div class="flex-1">
                <p class="metric-label">Total Consumers</p>
                <p class="metric-value-flat">{{ stats.totalConsumers }}</p>
              </div>
            </div>
          </div>
          <div class="metric-flat">
            <div class="flex items-start gap-3">
              <div class="metric-icon-flat bg-rose-500/10 dark:bg-rose-500/20">
                <svg class="w-6 h-6 text-rose-600 dark:text-rose-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 7h.01M7 3h5c.512 0 1.024.195 1.414.586l7 7a2 2 0 010 2.828l-7 7a2 2 0 01-2.828 0l-7-7A1.994 1.994 0 013 12V7a4 4 0 014-4z" />
                </svg>
              </div>
              <div class="flex-1">
                <p class="metric-label">Topics Monitored</p>
                <p class="metric-value-flat">{{ stats.totalTopics }}</p>
              </div>
            </div>
          </div>
          <div class="metric-flat">
            <div class="flex items-start gap-3">
              <div class="metric-icon-flat bg-orange-500/10 dark:bg-orange-500/20">
                <svg class="w-6 h-6 text-orange-600 dark:text-orange-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
                </svg>
              </div>
              <div class="flex-1">
                <p class="metric-label">Avg Lag</p>
                <p class="metric-value-flat">{{ stats.avgLag }}</p>
              </div>
            </div>
          </div>
        </div>

        <!-- Consumer Groups Table -->
        <div class="table-section">
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
                <tr v-for="group in filteredGroups" :key="group.name">
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
.page-flat {
  min-height: 100%;
}

.info-banner {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  color: inherit;
  border-left: 3px solid rgba(168, 85, 247, 0.3);
}

.dark .info-banner {
  background: rgba(255, 255, 255, 0.03);
  border-left-color: rgba(168, 85, 247, 0.5);
}

.filter-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
}

.dark .filter-flat {
  background: rgba(255, 255, 255, 0.03);
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

.metric-flat {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  transition: all 0.3s ease;
}

.dark .metric-flat {
  background: rgba(255, 255, 255, 0.03);
}

.metric-flat:hover {
  background: #fafafa;
}

.dark .metric-flat:hover {
  background: rgba(255, 255, 255, 0.05);
}

.metric-value-flat {
  font-size: 1.75rem;
  font-weight: 700;
  line-height: 1.1;
  margin-top: 0.25rem;
  background: linear-gradient(135deg, #f43f5e 0%, #ec4899 50%, #a855f7 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  letter-spacing: -0.02em;
}

.table-section {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem 0;
}

.dark .table-section {
  background: rgba(255, 255, 255, 0.03);
}

.metric-icon-flat {
  width: 2.5rem;
  height: 2.5rem;
  border-radius: 0.625rem;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.3s ease;
}

.metric-flat:hover .metric-icon-flat {
  transform: scale(1.05);
}

/* Flat table styling */
.table-section :deep(.table) {
  border-collapse: separate;
  border-spacing: 0;
}

.table-section :deep(.table thead) {
  background: transparent;
  border-bottom: 1px solid rgba(156, 163, 175, 0.08);
}

.dark .table-section :deep(.table thead) {
  border-bottom-color: rgba(156, 163, 175, 0.1);
}

.table-section :deep(.table th) {
  padding: 1rem 1rem;
  font-weight: 600;
  letter-spacing: 0.05em;
}

.table-section :deep(.table tbody tr) {
  border: none;
  transition: all 0.15s ease;
}

.table-section :deep(.table tbody tr:nth-child(even)) {
  background: rgba(0, 0, 0, 0.015);
}

.dark .table-section :deep(.table tbody tr:nth-child(even)) {
  background: rgba(255, 255, 255, 0.02);
}

.table-section :deep(.table tbody tr:hover) {
  background: rgba(244, 63, 94, 0.03);
  box-shadow: inset 3px 0 0 0 rgba(244, 63, 94, 0.6);
}

.dark .table-section :deep(.table tbody tr:hover) {
  background: rgba(244, 63, 94, 0.05);
  box-shadow: inset 3px 0 0 0 rgba(244, 63, 94, 0.8);
}

.table-section :deep(.table td) {
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
