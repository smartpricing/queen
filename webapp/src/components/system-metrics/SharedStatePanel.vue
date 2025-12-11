<template>
  <div class="shared-state-panel">
    <!-- Header -->
    <div class="ss-header">
      <div class="flex items-center gap-2">
        <svg class="w-4 h-4 text-violet-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
          <path stroke-linecap="round" stroke-linejoin="round" d="M7.5 21L3 16.5m0 0L7.5 12M3 16.5h13.5m0-13.5L21 7.5m0 0L16.5 12M21 7.5H7.5" />
        </svg>
        <span class="ss-title">Operations &amp; State</span>
        <span v-if="stats?.enabled" class="ss-badge ss-badge-cluster">Cluster Mode</span>
        <span v-else class="ss-badge ss-badge-single">Single Instance</span>
      </div>
    </div>
    
    <!-- Main Content -->
    <div class="ss-content">
      <!-- Queue Config Cache -->
      <div class="ss-section">
        <h3 class="ss-section-title">
          <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2 1 3 3 3h10c2 0 3-1 3-3V7c0-2-1-3-3-3H7c-2 0-3 1-3 3z" />
          </svg>
          Queue Configs
        </h3>
        <div class="ss-cards-mini">
          <div class="ss-card-mini ss-card-blue">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">Cache</span>
              <span :class="['ss-hit-rate', getHitRateClass(stats?.queue_config_cache?.hit_rate)]">
                {{ formatPercent(stats?.queue_config_cache?.hit_rate) }}
              </span>
            </div>
            <div class="ss-card-mini-stats">
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ stats?.queue_config_cache?.size || 0 }}</span>
                <span class="ss-mini-label">cached</span>
              </div>
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ formatK(stats?.queue_config_cache?.hits) }}</span>
                <span class="ss-mini-label">hits</span>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Servers -->
      <div class="ss-section">
        <h3 class="ss-section-title">
          <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 12h14M5 12a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v4a2 2 0 01-2 2M5 12a2 2 0 00-2 2v4a2 2 0 002 2h14a2 2 0 002-2v-4a2 2 0 00-2-2m-2-4h.01M17 16h.01" />
          </svg>
          Servers
        </h3>
        <div class="ss-cards-mini">
          <div class="ss-card-mini ss-card-green">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">Health</span>
            </div>
            <div class="ss-card-mini-stats">
              <div class="ss-mini-stat">
                <span class="ss-mini-value text-emerald-500">{{ stats?.server_health?.servers_alive || 0 }}</span>
                <span class="ss-mini-label">alive</span>
              </div>
              <div class="ss-mini-stat">
                <span class="ss-mini-value text-red-500">{{ stats?.server_health?.servers_dead || 0 }}</span>
                <span class="ss-mini-label">dead</span>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <!-- UDP Transport -->
      <div class="ss-section">
        <h3 class="ss-section-title">
          <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8.111 16.404a5.5 5.5 0 017.778 0M12 20h.01m-7.08-7.071c3.904-3.905 10.236-3.905 14.14 0M1.394 9.393c5.857-5.857 15.355-5.857 21.213 0" />
          </svg>
          UDP Transport
        </h3>
        <div class="ss-cards-mini">
          <div class="ss-card-mini ss-card-gray">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">Messages</span>
            </div>
            <div class="ss-card-mini-stats">
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ formatK(stats?.transport?.messages_sent) }}</span>
                <span class="ss-mini-label">sent</span>
              </div>
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ formatK(stats?.transport?.messages_received) }}</span>
                <span class="ss-mini-label">recv</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  data: {
    type: Object,
    default: null
  },
  timeSeriesData: {
    type: Object,
    default: null
  },
  loading: {
    type: Boolean,
    default: false
  }
});

// Helper to extract value from either flat number or aggregated object {avg, min, max, last}
function extractValue(val, key = 'last') {
  if (val === undefined || val === null) return 0;
  if (typeof val === 'number') return val;
  if (typeof val === 'object') return val[key] || val.last || val.avg || 0;
  return 0;
}

const stats = computed(() => {
  const ss = props.data;
  if (!ss) return { enabled: false };
  
  const calcHitRate = (hits, misses) => {
    const h = extractValue(hits);
    const m = extractValue(misses);
    const total = h + m;
    return total > 0 ? h / total : 1.0;
  };
  
  return {
    enabled: ss.enabled || false,
    queue_config_cache: {
      size: extractValue(ss.queue_config_cache?.size),
      hits: extractValue(ss.queue_config_cache?.hits),
      misses: extractValue(ss.queue_config_cache?.misses),
      hit_rate: calcHitRate(ss.queue_config_cache?.hits, ss.queue_config_cache?.misses)
    },
    server_health: {
      // Handle both formats: "alive"/"dead" (stored) and "servers_alive"/"servers_dead" (live)
      servers_alive: extractValue(ss.server_health?.alive) || extractValue(ss.server_health?.servers_alive),
      servers_dead: extractValue(ss.server_health?.dead) || extractValue(ss.server_health?.servers_dead)
    },
    transport: {
      // Handle both formats: "sent"/"received" (stored) and "messages_sent"/"messages_received" (live)
      messages_sent: extractValue(ss.transport?.sent) || extractValue(ss.transport?.messages_sent),
      messages_received: extractValue(ss.transport?.received) || extractValue(ss.transport?.messages_received),
      messages_dropped: extractValue(ss.transport?.dropped) || extractValue(ss.transport?.messages_dropped)
    }
  };
});

function formatPercent(value) {
  if (value === undefined || value === null) return '0%';
  return (value * 100).toFixed(0) + '%';
}

function formatK(value) {
  if (value === undefined || value === null) return '0';
  if (value >= 1000000) return (value / 1000000).toFixed(1) + 'M';
  if (value >= 1000) return (value / 1000).toFixed(1) + 'K';
  return Math.round(value).toString();
}

function getHitRateClass(rate) {
  if (rate === undefined || rate === null) return '';
  if (rate >= 0.9) return 'text-emerald-500';
  if (rate >= 0.7) return 'text-amber-500';
  return 'text-red-500';
}
</script>

<style scoped>
.shared-state-panel {
  @apply bg-white dark:bg-[#161b22] border border-gray-200/40 dark:border-gray-800/40;
  @apply rounded-lg p-3;
  box-shadow: 0 1px 2px 0 rgba(0, 0, 0, 0.03);
}

.ss-header {
  @apply flex items-center justify-between mb-3 pb-2 border-b border-gray-200/60 dark:border-gray-700/60;
}

.ss-title { @apply text-xs font-semibold text-gray-700 dark:text-gray-300; }

.ss-badge { @apply px-1.5 py-0.5 rounded text-[10px] font-semibold; }
.ss-badge-cluster { @apply bg-emerald-100 text-emerald-700 dark:bg-emerald-900/30 dark:text-emerald-400; }
.ss-badge-single { @apply bg-blue-100 text-blue-700 dark:bg-blue-900/30 dark:text-blue-400; }

.ss-content { @apply grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4; }
.ss-section { @apply space-y-2; }
.ss-section-title { @apply flex items-center gap-1.5 text-[10px] font-semibold text-gray-500 dark:text-gray-500 uppercase tracking-wide; }

.ss-cards-mini { @apply grid grid-cols-1 gap-2; }
.ss-card-mini { @apply p-2 rounded-lg border; }
.ss-card-blue { @apply bg-blue-50/50 border-blue-200/40 dark:bg-blue-900/10 dark:border-blue-800/30; }
.ss-card-green { @apply bg-emerald-50/50 border-emerald-200/40 dark:bg-emerald-900/10 dark:border-emerald-800/30; }
.ss-card-gray { @apply bg-gray-50/50 border-gray-200/40 dark:bg-gray-800/20 dark:border-gray-700/30; }
.ss-card-mini-header { @apply flex items-center justify-between mb-1; }
.ss-card-mini-title { @apply text-[10px] font-semibold text-gray-600 dark:text-gray-400 uppercase tracking-wide; }
.ss-card-mini-stats { @apply flex justify-between gap-2; }
.ss-mini-stat { @apply flex flex-col; }
.ss-mini-value { @apply text-sm font-bold font-mono text-gray-900 dark:text-gray-100; }
.ss-mini-label { @apply text-[8px] text-gray-500 dark:text-gray-500 uppercase; }

.ss-hit-rate { @apply text-xs font-bold; }
</style>
