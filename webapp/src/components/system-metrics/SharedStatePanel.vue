<template>
  <div class="info-card-white">
    <!-- Header -->
    <div class="shared-state-header">
      <div class="flex items-center gap-3">
        <div class="header-icon">
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.5">
            <path stroke-linecap="round" stroke-linejoin="round" d="M7.5 21L3 16.5m0 0L7.5 12M3 16.5h13.5m0-13.5L21 7.5m0 0L16.5 12M21 7.5H7.5" />
          </svg>
        </div>
        <div>
          <h3 class="text-base font-semibold text-gray-900 dark:text-white">Distributed Shared State</h3>
          <p class="text-xs text-gray-500 dark:text-gray-400">UDPSYNC Cache Layer</p>
        </div>
      </div>
      
      <div class="flex items-center gap-2">
        <span :class="['status-badge', stats?.enabled ? 'status-enabled' : 'status-disabled']">
          {{ stats?.enabled ? 'Enabled' : 'Disabled' }}
        </span>
        <span v-if="stats?.running" class="status-badge status-running">
          Running
        </span>
      </div>
    </div>
    
    <!-- Loading State -->
    <div v-if="loading" class="loading-state">
      <div class="animate-pulse space-y-4">
        <div class="h-4 bg-gray-200 dark:bg-gray-700 rounded w-1/3"></div>
        <div class="grid grid-cols-2 lg:grid-cols-4 gap-4">
          <div v-for="i in 4" :key="i" class="h-24 bg-gray-200 dark:bg-gray-700 rounded"></div>
        </div>
      </div>
    </div>
    
    <!-- Disabled State -->
    <div v-else-if="!stats?.enabled" class="disabled-state">
      <div class="flex flex-col items-center justify-center py-8 text-center">
        <svg class="w-12 h-12 text-gray-300 dark:text-gray-600 mb-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M18.364 18.364A9 9 0 005.636 5.636m12.728 12.728A9 9 0 015.636 5.636m12.728 12.728L5.636 5.636" />
        </svg>
        <p class="text-sm text-gray-500 dark:text-gray-400">Shared state synchronization is disabled</p>
        <p class="text-xs text-gray-400 dark:text-gray-500 mt-1">Configure QUEEN_UDP_PEERS to enable multi-instance sync</p>
      </div>
    </div>
    
    <!-- Stats Content -->
    <div v-else class="stats-content">
      <!-- Server Info -->
      <div class="server-info">
        <span class="text-xs text-gray-500 dark:text-gray-400">Server ID:</span>
        <span class="font-mono text-sm text-gray-900 dark:text-white">{{ stats.server_id }}</span>
      </div>
      
      <!-- Cache Stats Grid -->
      <div class="cache-grid">
        <!-- Queue Config Cache -->
        <div class="cache-card cache-card-config">
          <div class="cache-header">
            <div class="cache-icon cache-icon-config">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6h16M4 10h16M4 14h16M4 18h16" />
              </svg>
            </div>
            <h4 class="cache-title">Queue Configs</h4>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Cached</span>
            <span class="cache-value">{{ stats.queue_config_cache?.size || 0 }}</span>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Hit Rate</span>
            <span :class="['cache-value', getHitRateClass(stats.queue_config_cache?.hit_rate)]">
              {{ formatPercent(stats.queue_config_cache?.hit_rate) }}
            </span>
          </div>
          <div class="cache-stat-row text-xs">
            <span class="cache-label">Hits / Misses</span>
            <span class="cache-value font-mono">
              {{ stats.queue_config_cache?.hits || 0 }} / {{ stats.queue_config_cache?.misses || 0 }}
            </span>
          </div>
        </div>
        
        <!-- Consumer Presence -->
        <div class="cache-card cache-card-presence">
          <div class="cache-header">
            <div class="cache-icon cache-icon-presence">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0z" />
              </svg>
            </div>
            <h4 class="cache-title">Consumer Presence</h4>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Queues Tracked</span>
            <span class="cache-value">{{ stats.consumer_presence?.queues_tracked || 0 }}</span>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Servers</span>
            <span class="cache-value">{{ stats.consumer_presence?.servers_tracked || 0 }}</span>
          </div>
          <div class="cache-stat-row text-xs">
            <span class="cache-label">Reg / Dereg</span>
            <span class="cache-value font-mono">
              {{ stats.consumer_presence?.registrations_received || 0 }} / {{ stats.consumer_presence?.deregistrations_received || 0 }}
            </span>
          </div>
        </div>
        
        <!-- Partition ID Cache -->
        <div class="cache-card cache-card-partition">
          <div class="cache-header">
            <div class="cache-icon cache-icon-partition">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4" />
              </svg>
            </div>
            <h4 class="cache-title">Partition IDs</h4>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Cached</span>
            <span class="cache-value">{{ stats.partition_id_cache?.size || 0 }} / {{ stats.partition_id_cache?.max_size || 0 }}</span>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Hit Rate</span>
            <span :class="['cache-value', getHitRateClass(stats.partition_id_cache?.hit_rate)]">
              {{ formatPercent(stats.partition_id_cache?.hit_rate) }}
            </span>
          </div>
          <div class="cache-stat-row text-xs">
            <span class="cache-label">Evictions</span>
            <span class="cache-value font-mono">{{ stats.partition_id_cache?.evictions || 0 }}</span>
          </div>
        </div>
        
        <!-- Lease Hints -->
        <div class="cache-card cache-card-lease">
          <div class="cache-header">
            <div class="cache-icon cache-icon-lease">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 7a2 2 0 012 2m4 0a6 6 0 01-7.743 5.743L11 17H9v2H7v2H4a1 1 0 01-1-1v-2.586a1 1 0 01.293-.707l5.964-5.964A6 6 0 1121 9z" />
              </svg>
            </div>
            <h4 class="cache-title">Lease Hints</h4>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Cached</span>
            <span class="cache-value">{{ stats.lease_hints?.size || 0 }}</span>
          </div>
          <div class="cache-stat-row">
            <span class="cache-label">Accuracy</span>
            <span :class="['cache-value', getHitRateClass(stats.lease_hints?.accuracy)]">
              {{ formatPercent(stats.lease_hints?.accuracy) }}
            </span>
          </div>
          <div class="cache-stat-row text-xs">
            <span class="cache-label">Used / Wrong</span>
            <span class="cache-value font-mono">
              {{ stats.lease_hints?.hints_used || 0 }} / {{ stats.lease_hints?.hints_wrong || 0 }}
            </span>
          </div>
        </div>
      </div>
      
      <!-- Server Health Section -->
      <div class="health-section">
        <h4 class="section-title">
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m5.618-4.016A11.955 11.955 0 0112 2.944a11.955 11.955 0 01-8.618 3.04A12.02 12.02 0 003 9c0 5.591 3.824 10.29 9 11.622 5.176-1.332 9-6.03 9-11.622 0-1.042-.133-2.052-.382-3.016z" />
          </svg>
          Server Health
        </h4>
        <div class="health-stats">
          <div class="health-stat">
            <span class="health-value text-emerald-500">{{ stats.server_health?.servers_alive || 0 }}</span>
            <span class="health-label">Alive</span>
          </div>
          <div class="health-stat">
            <span class="health-value text-red-500">{{ stats.server_health?.servers_dead || 0 }}</span>
            <span class="health-label">Dead</span>
          </div>
          <div class="health-stat">
            <span class="health-value text-gray-500">{{ stats.server_health?.heartbeats_received || 0 }}</span>
            <span class="health-label">Heartbeats</span>
          </div>
          <div class="health-stat">
            <span class="health-value text-amber-500">{{ stats.server_health?.restarts_detected || 0 }}</span>
            <span class="health-label">Restarts</span>
          </div>
        </div>
      </div>
      
      <!-- Transport Stats (if available) -->
      <div v-if="stats.transport" class="transport-section">
        <h4 class="section-title">
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 12h.01M12 12h.01M16 12h.01M21 12c0 4.418-4.03 8-9 8a9.863 9.863 0 01-4.255-.949L3 20l1.395-3.72C3.512 15.042 3 13.574 3 12c0-4.418 4.03-8 9-8s9 3.582 9 8z" />
          </svg>
          UDP Transport
        </h4>
        <div class="transport-stats">
          <div class="transport-stat">
            <span class="transport-value">{{ stats.transport?.messages_sent || 0 }}</span>
            <span class="transport-label">Sent</span>
          </div>
          <div class="transport-stat">
            <span class="transport-value">{{ stats.transport?.messages_received || 0 }}</span>
            <span class="transport-label">Received</span>
          </div>
          <div class="transport-stat">
            <span class="transport-value text-red-400">{{ stats.transport?.messages_invalid || 0 }}</span>
            <span class="transport-label">Invalid</span>
          </div>
          <div class="transport-stat">
            <span class="transport-value">{{ stats.transport?.peers?.length || 0 }}</span>
            <span class="transport-label">Peers</span>
          </div>
        </div>
        
        <!-- Peer List -->
        <div v-if="stats.transport?.peers?.length" class="peers-list">
          <div v-for="(peer, idx) in stats.transport.peers" :key="idx" class="peer-item">
            <span class="peer-id">{{ peer.learned_server_id || `${peer.hostname}:${peer.port}` }}</span>
            <span :class="['peer-status', peer.resolved ? 'peer-resolved' : 'peer-unresolved']">
              {{ peer.resolved ? '●' : '○' }}
            </span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue';
import { systemMetricsApi } from '../../api/system-metrics';

const stats = ref(null);
const loading = ref(true);
const error = ref(null);
let refreshInterval = null;

function formatPercent(value) {
  if (value === undefined || value === null) return '0%';
  return (value * 100).toFixed(1) + '%';
}

function getHitRateClass(rate) {
  if (rate === undefined || rate === null) return '';
  if (rate >= 0.9) return 'text-emerald-500';
  if (rate >= 0.7) return 'text-amber-500';
  return 'text-red-500';
}

async function fetchStats() {
  try {
    const response = await systemMetricsApi.getSharedStateStats();
    stats.value = response.data;
    error.value = null;
  } catch (err) {
    console.error('Failed to fetch shared state stats:', err);
    error.value = err.message;
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  fetchStats();
  // Refresh every 5 seconds
  refreshInterval = setInterval(fetchStats, 5000);
});

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval);
  }
});
</script>

<style scoped>
.shared-state-header {
  @apply flex items-center justify-between pb-4 mb-4 border-b border-gray-200 dark:border-gray-700;
}

.header-icon {
  @apply w-10 h-10 rounded-lg bg-gradient-to-br from-violet-500 to-purple-600 flex items-center justify-center text-white shadow-lg shadow-violet-500/20;
}

.status-badge {
  @apply px-2.5 py-1 rounded-full text-xs font-semibold;
}

.status-enabled {
  @apply bg-emerald-100 text-emerald-700 dark:bg-emerald-900/30 dark:text-emerald-400;
}

.status-disabled {
  @apply bg-gray-100 text-gray-600 dark:bg-gray-700 dark:text-gray-400;
}

.status-running {
  @apply bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400;
}

.loading-state, .disabled-state {
  @apply py-4;
}

.stats-content {
  @apply space-y-5;
}

.server-info {
  @apply flex items-center gap-2 pb-4 border-b border-gray-200 dark:border-gray-700;
}

.cache-grid {
  @apply grid grid-cols-2 lg:grid-cols-4 gap-4;
}

.cache-card {
  @apply p-4 rounded-lg border border-gray-200/60 dark:border-gray-700/60 space-y-2;
}

.cache-card-config {
  @apply bg-gradient-to-br from-blue-50 to-blue-100/50 dark:from-blue-900/20 dark:to-blue-800/10;
}

.cache-card-presence {
  @apply bg-gradient-to-br from-emerald-50 to-emerald-100/50 dark:from-emerald-900/20 dark:to-emerald-800/10;
}

.cache-card-partition {
  @apply bg-gradient-to-br from-amber-50 to-amber-100/50 dark:from-amber-900/20 dark:to-amber-800/10;
}

.cache-card-lease {
  @apply bg-gradient-to-br from-purple-50 to-purple-100/50 dark:from-purple-900/20 dark:to-purple-800/10;
}

.cache-header {
  @apply flex items-center gap-2 pb-2 border-b border-gray-200/60 dark:border-gray-700/40;
}

.cache-icon {
  @apply w-7 h-7 rounded-md flex items-center justify-center text-white;
}

.cache-icon-config {
  @apply bg-blue-500;
}

.cache-icon-presence {
  @apply bg-emerald-500;
}

.cache-icon-partition {
  @apply bg-amber-500;
}

.cache-icon-lease {
  @apply bg-purple-500;
}

.cache-title {
  @apply text-sm font-semibold text-gray-700 dark:text-gray-200;
}

.cache-stat-row {
  @apply flex items-center justify-between;
}

.cache-label {
  @apply text-xs text-gray-500 dark:text-gray-400;
}

.cache-value {
  @apply text-sm font-semibold text-gray-900 dark:text-white;
}

.section-title {
  @apply flex items-center gap-2 text-sm font-semibold text-gray-700 dark:text-gray-200 mb-3;
}

.health-section, .transport-section {
  @apply pt-4 border-t border-gray-200 dark:border-gray-700;
}

.health-stats, .transport-stats {
  @apply grid grid-cols-4 gap-4;
}

.health-stat, .transport-stat {
  @apply flex flex-col items-center text-center;
}

.health-value, .transport-value {
  @apply text-xl font-bold;
}

.health-label, .transport-label {
  @apply text-xs text-gray-500 dark:text-gray-400 mt-1;
}

.peers-list {
  @apply mt-4 space-y-1;
}

.peer-item {
  @apply flex items-center justify-between px-3 py-2 bg-gray-50 dark:bg-gray-800/50 rounded-lg text-sm;
}

.peer-id {
  @apply font-mono text-sm text-gray-900 dark:text-gray-100;
}

.peer-status {
  @apply text-lg;
}

.peer-resolved {
  @apply text-emerald-500;
}

.peer-unresolved {
  @apply text-gray-400;
}
</style>


