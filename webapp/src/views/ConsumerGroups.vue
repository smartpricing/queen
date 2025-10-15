<template>
  <div class="p-4 sm:p-6">
    <div class="space-y-4 sm:space-y-6 max-w-7xl mx-auto">
      <!-- Info Card -->
      <div class="card bg-purple-50 dark:bg-purple-900/20 text-purple-900 dark:text-purple-100">
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
      <div class="card">
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

      <div v-else-if="error" class="card bg-red-50 dark:bg-red-900/20 text-red-600 text-sm">
        <p><strong>Error loading consumer groups:</strong> {{ error }}</p>
      </div>

      <!-- Consumer Groups Stats -->
      <div class="grid grid-cols-2 lg:grid-cols-4 gap-3 sm:gap-4">
        <MetricCard
          title="Active Groups"
          :value="stats.activeGroups"
        />
        <MetricCard
          title="Total Consumers"
          :value="stats.totalConsumers"
        />
        <MetricCard
          title="Topics Monitored"
          :value="stats.totalTopics"
        />
        <MetricCard
          title="Avg Lag"
          :value="stats.avgLag"
          secondary-label="messages"
        />
      </div>

      <!-- Consumer Groups Table -->
      <div class="card">
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
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { consumersApi } from '../api/consumers';
import { formatNumber, formatDuration } from '../utils/formatters';

import MetricCard from '../components/common/MetricCard.vue';
import StatusBadge from '../components/common/StatusBadge.vue';
import LoadingSpinner from '../components/common/LoadingSpinner.vue';

const loading = ref(false);
const error = ref(null);
const consumerGroups = ref([]);
const searchQuery = ref('');
const statusFilter = ref('');

const filteredGroups = computed(() => {
  let filtered = consumerGroups.value;
  
  // Filter by search
  if (searchQuery.value) {
    const search = searchQuery.value.toLowerCase();
    filtered = filtered.filter(g => 
      g.name.toLowerCase().includes(search) ||
      g.topics.some(t => t.toLowerCase().includes(search))
    );
  }
  
  // Filter by status
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
  
  // Register refresh callback for header button
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
