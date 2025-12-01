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
      <!-- Left Column: Operation Metrics -->
      <div class="ss-section">
        <h3 class="ss-section-title">
          <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
          </svg>
          Sidecar Operations
        </h3>
        <div class="ss-ops-grid">
          <!-- PUSH -->
          <div class="ss-op-card ss-op-push">
            <div class="ss-op-header">
              <span class="ss-op-name">PUSH</span>
              <span class="ss-op-count">{{ formatK(sidecarOps.push?.count) }}</span>
            </div>
            <div class="ss-op-details">
              <div class="ss-op-stat">
                <span class="ss-op-value">{{ formatLatency(sidecarOps.push?.latency_us) }}</span>
                <span class="ss-op-label">avg latency</span>
              </div>
              <div class="ss-op-stat">
                <span class="ss-op-value">{{ formatK(sidecarOps.push?.items) }}</span>
                <span class="ss-op-label">items</span>
              </div>
            </div>
          </div>
          
          <!-- POP -->
          <div class="ss-op-card ss-op-pop">
            <div class="ss-op-header">
              <span class="ss-op-name">POP</span>
              <span class="ss-op-count">{{ formatK(sidecarOps.pop?.count) }}</span>
            </div>
            <div class="ss-op-details">
              <div class="ss-op-stat">
                <span class="ss-op-value">{{ formatLatency(sidecarOps.pop?.latency_us) }}</span>
                <span class="ss-op-label">avg latency</span>
              </div>
              <div class="ss-op-stat">
                <span class="ss-op-value">{{ formatK(sidecarOps.pop?.items) }}</span>
                <span class="ss-op-label">items</span>
              </div>
            </div>
          </div>
          
          <!-- ACK -->
          <div class="ss-op-card ss-op-ack">
            <div class="ss-op-header">
              <span class="ss-op-name">ACK</span>
              <span class="ss-op-count">{{ formatK(sidecarOps.ack?.count) }}</span>
            </div>
            <div class="ss-op-details">
              <div class="ss-op-stat">
                <span class="ss-op-value">{{ formatLatency(sidecarOps.ack?.latency_us) }}</span>
                <span class="ss-op-label">avg latency</span>
              </div>
              <div class="ss-op-stat">
                <span class="ss-op-value">{{ formatK(sidecarOps.ack?.items) }}</span>
                <span class="ss-op-label">items</span>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Middle Column: Cache & Summary -->
      <div class="ss-section">
        <h3 class="ss-section-title">
          <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2 1 3 3 3h10c2 0 3-1 3-3V7c0-2-1-3-3-3H7c-2 0-3 1-3 3z" />
          </svg>
          Summary
        </h3>
        
        <div class="ss-cards-mini">
          <!-- Backoff Summary -->
          <div class="ss-card-mini ss-card-amber">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">Backoff</span>
              <span :class="['ss-backoff-indicator', getBackoffClass(queueBackoff.total_backed_off_groups)]">
                {{ queueBackoff.total_backed_off_groups || 0 }} groups
              </span>
            </div>
            <div class="ss-card-mini-stats">
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ queueBackoff.queues_with_backoff || 0 }}</span>
                <span class="ss-mini-label">queues</span>
              </div>
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ queueBackoff.avg_interval_ms || 0 }}ms</span>
                <span class="ss-mini-label">interval</span>
              </div>
            </div>
          </div>
          
          <!-- Queue Config Cache -->
          <div class="ss-card-mini ss-card-blue">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">Queue Configs</span>
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
      
      <!-- Right Column: Cluster Health (only in cluster mode) -->
      <div class="ss-section" v-if="stats?.enabled">
        <h3 class="ss-section-title">
          <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 12a9 9 0 01-9 9m9-9a9 9 0 00-9-9m9 9H3m9 9a9 9 0 01-9-9m9 9c1.657 0 3-4.03 3-9s-1.343-9-3-9m0 18c-1.657 0-3-4.03-3-9s1.343-9 3-9m-9 9a9 9 0 019-9" />
          </svg>
          Cluster Health
        </h3>
        <div class="ss-cards-mini">
          <!-- Server Health -->
          <div class="ss-card-mini ss-card-green">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">Servers</span>
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
          
          <!-- Consumer Presence -->
          <div class="ss-card-mini ss-card-purple">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">Consumers</span>
            </div>
            <div class="ss-card-mini-stats">
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ stats?.consumer_presence?.queues_tracked || 0 }}</span>
                <span class="ss-mini-label">queues</span>
              </div>
              <div class="ss-mini-stat">
                <span class="ss-mini-value">{{ stats?.consumer_presence?.servers_tracked || 0 }}</span>
                <span class="ss-mini-label">servers</span>
              </div>
            </div>
          </div>
          
          <!-- UDP Transport -->
          <div class="ss-card-mini ss-card-gray">
            <div class="ss-card-mini-header">
              <span class="ss-card-mini-title">UDP Transport</span>
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
      
      <!-- Right Column: Single Instance Info -->
      <div class="ss-section ss-single-info" v-else>
        <h3 class="ss-section-title">
          <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          Single Instance Mode
        </h3>
        <div class="ss-info-box">
          <p class="ss-info-text">
            Running in single instance mode. Set <code>QUEEN_UDP_PEERS</code> to enable cluster sync.
          </p>
          <div class="ss-info-example">
            <code>QUEEN_UDP_PEERS=queen-1:6634,queen-2:6634</code>
          </div>
        </div>
      </div>
    </div>
    
    <!-- Queue Backoff Details Card (Full Width) -->
    <div class="ss-backoff-card" v-if="allServersBackoff.length > 0">
      <div class="ss-backoff-header">
        <div class="flex items-center gap-2">
          <svg class="w-4 h-4 text-amber-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
          </svg>
          <span class="ss-backoff-title">Queue Backoff by Server</span>
        </div>
        <div class="ss-backoff-summary">
          <span class="ss-backoff-total">{{ totalBackedOffGroups }} groups backed off</span>
          <span class="ss-backoff-servers">across {{ allServersBackoff.length }} server(s)</span>
        </div>
      </div>
      
      <div class="ss-servers-grid">
        <div v-for="server in allServersBackoff" :key="server.serverId" class="ss-server-card">
          <div class="ss-server-header">
            <span class="ss-server-name">{{ server.serverName }}</span>
            <span :class="['ss-server-badge', server.totalBackedOff > 0 ? 'ss-badge-warn' : 'ss-badge-ok']">
              {{ server.totalBackedOff }} backed off
            </span>
          </div>
          
          <div class="ss-server-queues" v-if="server.queues.length > 0">
            <div 
              v-for="q in server.queues" 
              :key="q.queue_name"
              :class="['ss-queue-row', q.groups_backed_off > 0 ? 'ss-queue-warn' : 'ss-queue-ok']"
            >
              <span class="ss-queue-label" :title="q.queue_name">{{ formatQueueName(q.queue_name) }}</span>
              <div class="ss-queue-metrics">
                <span class="ss-metric">
                  <span :class="q.groups_backed_off > 0 ? 'text-amber-600 font-semibold' : 'text-emerald-600'">
                    {{ q.groups_backed_off }}
                  </span>
                  <span class="text-gray-400">/{{ q.groups_tracked }}</span>
                </span>
                <span class="ss-metric-interval">{{ q.avg_interval_ms }}ms</span>
              </div>
            </div>
          </div>
          <div v-else class="ss-server-empty">No queue data</div>
        </div>
      </div>
    </div>
    
    <!-- Bottom: Operations Chart -->
    <div class="ss-chart-section" v-if="chartData">
      <div class="ss-chart-header">
        <span class="ss-chart-title">Operations Over Time</span>
        <div class="ss-chart-legend">
          <span class="legend-item"><span class="legend-dot bg-emerald-500"></span>PUSH</span>
          <span class="legend-item"><span class="legend-dot bg-blue-500"></span>POP</span>
          <span class="legend-item"><span class="legend-dot bg-amber-500"></span>ACK</span>
        </div>
      </div>
      <div class="ss-chart-container">
        <Line :data="chartData" :options="chartOptions" />
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { Line } from 'vue-chartjs';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler,
} from 'chart.js';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler
);

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
  if (!props.data) return null;
  
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
    consumer_presence: {
      queues_tracked: extractValue(ss.consumer_presence?.queues_tracked),
      servers_tracked: extractValue(ss.consumer_presence?.servers_tracked),
      total_registrations: extractValue(ss.consumer_presence?.total_registrations)
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

const sidecarOps = computed(() => {
  if (!props.data?.sidecar_ops) {
    return {
      push: { count: 0, latency_us: 0, items: 0 },
      pop: { count: 0, latency_us: 0, items: 0 },
      ack: { count: 0, latency_us: 0, items: 0 }
    };
  }
  
  const ops = props.data.sidecar_ops;
  return {
    push: {
      count: extractValue(ops.push?.count),
      latency_us: extractValue(ops.push?.latency_us) || extractValue(ops.push?.avg_latency_us),
      items: extractValue(ops.push?.items)
    },
    pop: {
      count: extractValue(ops.pop?.count),
      latency_us: extractValue(ops.pop?.latency_us) || extractValue(ops.pop?.avg_latency_us),
      items: extractValue(ops.pop?.items)
    },
    ack: {
      count: extractValue(ops.ack?.count),
      latency_us: extractValue(ops.ack?.latency_us) || extractValue(ops.ack?.avg_latency_us),
      items: extractValue(ops.ack?.items)
    }
  };
});

const queueBackoff = computed(() => {
  if (!props.data?.queue_backoff) {
    return {
      queues_with_backoff: 0,
      total_backed_off_groups: 0,
      avg_interval_ms: 0
    };
  }
  
  const qb = props.data.queue_backoff;
  return {
    queues_with_backoff: extractValue(qb.queues_with_backoff),
    total_backed_off_groups: extractValue(qb.total_backed_off_groups),
    avg_interval_ms: extractValue(qb.avg_interval_ms)
  };
});

// Per-queue backoff list from queue_backoff_summary (if available)
const queueBackoffList = computed(() => {
  // Try to get from queue_backoff_summary array (from /health or stored metrics)
  const summary = props.data?.queue_backoff_summary;
  if (Array.isArray(summary)) {
    return summary.map(q => ({
      queue_name: q.queue || q.queue_name || 'unknown',
      groups_tracked: extractValue(q.groups_tracked),
      groups_in_flight: extractValue(q.groups_in_flight),
      groups_backed_off: extractValue(q.groups_backed_off),
      max_consecutive_empty: extractValue(q.max_consecutive_empty),
      avg_interval_ms: extractValue(q.avg_interval_ms)
    })).sort((a, b) => b.groups_backed_off - a.groups_backed_off); // Sort by most backed off
  }
  return [];
});

function formatQueueName(name) {
  if (!name) return 'unknown';
  // Shorten long queue names by showing last 2 segments
  const parts = name.split('.');
  if (parts.length > 2) {
    return '...' + parts.slice(-2).join('.');
  }
  return name;
}

// Per-server backoff data from all replicas
const allServersBackoff = computed(() => {
  if (!props.timeSeriesData?.replicas?.length) {
    // Fallback: use current data if no time series
    if (queueBackoffList.value.length > 0) {
      return [{
        serverId: 'current',
        serverName: 'This Server',
        totalBackedOff: queueBackoff.value.total_backed_off_groups,
        queues: queueBackoffList.value
      }];
    }
    return [];
  }
  
  const servers = [];
  
  for (const replica of props.timeSeriesData.replicas) {
    // Get the latest time point
    const lastPoint = replica.timeSeries?.[replica.timeSeries.length - 1];
    const summary = lastPoint?.metrics?.shared_state?.queue_backoff_summary;
    
    if (!summary || !Array.isArray(summary)) continue;
    
    const queues = summary.map(q => ({
      queue_name: q.queue || q.queue_name || 'unknown',
      groups_tracked: extractValue(q.groups_tracked),
      groups_in_flight: extractValue(q.groups_in_flight),
      groups_backed_off: extractValue(q.groups_backed_off),
      avg_interval_ms: extractValue(q.avg_interval_ms)
    })).sort((a, b) => b.groups_backed_off - a.groups_backed_off);
    
    const totalBackedOff = queues.reduce((sum, q) => sum + q.groups_backed_off, 0);
    
    servers.push({
      serverId: `${replica.hostname}:${replica.port}`,
      serverName: replica.hostname?.replace('.local', '') || `Server ${replica.port}`,
      totalBackedOff,
      queues
    });
  }
  
  // Sort by most backed off first
  return servers.sort((a, b) => b.totalBackedOff - a.totalBackedOff);
});

const totalBackedOffGroups = computed(() => {
  return allServersBackoff.value.reduce((sum, s) => sum + s.totalBackedOff, 0);
});

const chartData = computed(() => {
  if (!props.timeSeriesData?.replicas?.length) return null;
  
  const replicas = props.timeSeriesData.replicas;
  const allTimestamps = new Set();
  replicas.forEach(replica => {
    replica.timeSeries?.forEach(point => {
      allTimestamps.add(point.timestamp);
    });
  });
  
  if (allTimestamps.size === 0) return null;
  
  const sortedTimestamps = Array.from(allTimestamps).sort();
  const pushCounts = [];
  const popCounts = [];
  const ackCounts = [];
  
  sortedTimestamps.forEach(ts => {
    let pushTotal = 0, popTotal = 0, ackTotal = 0;
    
    replicas.forEach(replica => {
      const point = replica.timeSeries?.find(p => p.timestamp === ts);
      if (point?.metrics?.shared_state?.sidecar_ops) {
        const ops = point.metrics.shared_state.sidecar_ops;
        // Handle both formats: flat numbers or aggregated objects
        pushTotal += extractValue(ops.push?.count);
        popTotal += extractValue(ops.pop?.count);
        ackTotal += extractValue(ops.ack?.count);
      }
    });
    
    pushCounts.push(pushTotal);
    popCounts.push(popTotal);
    ackCounts.push(ackTotal);
  });
  
  const hasData = pushCounts.some(v => v > 0) || popCounts.some(v => v > 0) || ackCounts.some(v => v > 0);
  if (!hasData) return null;
  
  return {
    labels: sortedTimestamps.map(ts => formatTimestamp(ts)),
    datasets: [
      {
        label: 'PUSH',
        data: pushCounts,
        borderColor: 'rgba(16, 185, 129, 1)',
        backgroundColor: 'rgba(16, 185, 129, 0.08)',
        borderWidth: 2,
        fill: true,
        tension: 0.3,
        pointRadius: 0,
        pointHoverRadius: 4,
      },
      {
        label: 'POP',
        data: popCounts,
        borderColor: 'rgba(59, 130, 246, 1)',
        backgroundColor: 'rgba(59, 130, 246, 0.08)',
        borderWidth: 2,
        fill: true,
        tension: 0.3,
        pointRadius: 0,
        pointHoverRadius: 4,
      },
      {
        label: 'ACK',
        data: ackCounts,
        borderColor: 'rgba(245, 158, 11, 1)',
        backgroundColor: 'rgba(245, 158, 11, 0.08)',
        borderWidth: 2,
        fill: true,
        tension: 0.3,
        pointRadius: 0,
        pointHoverRadius: 4,
      },
    ],
  };
});

function formatTimestamp(ts) {
  const date = new Date(ts);
  return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
}

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: { mode: 'index', intersect: false },
  plugins: {
    legend: { display: false },
    tooltip: {
      backgroundColor: 'rgba(0, 0, 0, 0.9)',
      padding: 10,
      titleFont: { size: 11 },
      bodyFont: { size: 11 },
      callbacks: {
        label: function(context) {
          return context.dataset.label + ': ' + formatK(context.parsed.y);
        }
      }
    },
  },
  scales: {
    x: {
      grid: { display: false },
      ticks: { color: '#9ca3af', font: { size: 10 }, maxRotation: 0, autoSkipPadding: 30 },
    },
    y: {
      min: 0,
      grid: { color: 'rgba(0, 0, 0, 0.04)', drawBorder: false },
      ticks: { color: '#9ca3af', font: { size: 10 }, callback: (value) => formatK(value) },
    },
  },
};

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

function formatLatency(value) {
  if (value === undefined || value === null || value === 0) return '0us';
  if (value >= 1000000) return (value / 1000000).toFixed(1) + 's';
  if (value >= 1000) return (value / 1000).toFixed(1) + 'ms';
  return Math.round(value) + 'us';
}

function getHitRateClass(rate) {
  if (rate === undefined || rate === null) return '';
  if (rate >= 0.9) return 'text-emerald-500';
  if (rate >= 0.7) return 'text-amber-500';
  return 'text-red-500';
}

function getBackoffClass(count) {
  if (count === undefined || count === null || count === 0) return 'ss-backoff-ok';
  if (count <= 5) return 'ss-backoff-warn';
  return 'ss-backoff-high';
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

.ss-ops-grid { @apply grid grid-cols-1 gap-2; }
.ss-op-card { @apply p-2 rounded-lg border; }
.ss-op-push { @apply bg-emerald-50/50 border-emerald-200/40 dark:bg-emerald-900/10 dark:border-emerald-800/30; }
.ss-op-pop { @apply bg-blue-50/50 border-blue-200/40 dark:bg-blue-900/10 dark:border-blue-800/30; }
.ss-op-ack { @apply bg-amber-50/50 border-amber-200/40 dark:bg-amber-900/10 dark:border-amber-800/30; }
.ss-op-header { @apply flex items-center justify-between mb-1; }
.ss-op-name { @apply text-[10px] font-bold uppercase tracking-wider text-gray-600 dark:text-gray-400; }
.ss-op-count { @apply text-sm font-bold font-mono text-gray-900 dark:text-gray-100; }
.ss-op-details { @apply flex justify-between gap-2; }
.ss-op-stat { @apply flex flex-col; }
.ss-op-value { @apply text-xs font-semibold font-mono text-gray-700 dark:text-gray-300; }
.ss-op-label { @apply text-[8px] text-gray-500 dark:text-gray-500 uppercase; }

.ss-cards-mini { @apply grid grid-cols-1 gap-2; }
.ss-card-mini { @apply p-2 rounded-lg border; }
.ss-card-blue { @apply bg-blue-50/50 border-blue-200/40 dark:bg-blue-900/10 dark:border-blue-800/30; }
.ss-card-green { @apply bg-emerald-50/50 border-emerald-200/40 dark:bg-emerald-900/10 dark:border-emerald-800/30; }
.ss-card-amber { @apply bg-amber-50/50 border-amber-200/40 dark:bg-amber-900/10 dark:border-amber-800/30; }
.ss-card-purple { @apply bg-purple-50/50 border-purple-200/40 dark:bg-purple-900/10 dark:border-purple-800/30; }
.ss-card-gray { @apply bg-gray-50/50 border-gray-200/40 dark:bg-gray-800/20 dark:border-gray-700/30; }
.ss-card-mini-header { @apply flex items-center justify-between mb-1; }
.ss-card-mini-title { @apply text-[10px] font-semibold text-gray-600 dark:text-gray-400 uppercase tracking-wide; }
.ss-card-mini-stats { @apply flex justify-between gap-2; }
.ss-mini-stat { @apply flex flex-col; }
.ss-mini-value { @apply text-sm font-bold font-mono text-gray-900 dark:text-gray-100; }
.ss-mini-label { @apply text-[8px] text-gray-500 dark:text-gray-500 uppercase; }

.ss-hit-rate { @apply text-xs font-bold; }
.ss-backoff-indicator { @apply text-[10px] font-semibold px-1.5 py-0.5 rounded; }
.ss-backoff-ok { @apply bg-emerald-100 text-emerald-700 dark:bg-emerald-900/30 dark:text-emerald-400; }
.ss-backoff-warn { @apply bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400; }
.ss-backoff-high { @apply bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400; }

.ss-single-info { @apply lg:col-span-1; }
.ss-info-box { @apply p-3 rounded-lg bg-gray-50 dark:bg-gray-800/30 border border-gray-200/60 dark:border-gray-700/40; }
.ss-info-text { @apply text-xs text-gray-600 dark:text-gray-400 mb-2; }
.ss-info-text code { @apply bg-gray-200 dark:bg-gray-700 px-1 py-0.5 rounded text-[10px] font-mono; }
.ss-info-example { @apply bg-gray-900 dark:bg-gray-950 rounded p-2 overflow-x-auto; }
.ss-info-example code { @apply text-[10px] text-emerald-400 font-mono whitespace-nowrap; }

.ss-chart-section { @apply mt-4 pt-3 border-t border-gray-200/60 dark:border-gray-700/60; }
.ss-chart-header { @apply flex items-center justify-between mb-2; }
.ss-chart-title { @apply text-[10px] font-semibold text-gray-600 dark:text-gray-400 uppercase tracking-wide; }
.ss-chart-legend { @apply flex items-center gap-3; }
.legend-item { @apply flex items-center gap-1 text-[10px] text-gray-500 dark:text-gray-400; }
.legend-dot { @apply w-2 h-2 rounded-full; }
.ss-chart-container { height: 120px; }

/* Queue list styles */
.ss-section-badge { @apply ml-2 px-1.5 py-0.5 rounded text-[9px] font-semibold bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400; }
.ss-queue-list { @apply space-y-1 max-h-32 overflow-y-auto pr-1; }
.ss-queue-item { @apply flex items-center justify-between p-1.5 rounded text-[10px] border; }
.ss-queue-ok { @apply bg-emerald-50/30 border-emerald-200/30 dark:bg-emerald-900/5 dark:border-emerald-800/20; }
.ss-queue-backing-off { @apply bg-amber-50/50 border-amber-200/40 dark:bg-amber-900/10 dark:border-amber-800/30; }
.ss-queue-name { @apply font-medium text-gray-700 dark:text-gray-300 truncate max-w-[120px]; }
.ss-queue-stats { @apply flex items-center gap-2 text-[9px] font-mono; }
.ss-queue-stat { @apply whitespace-nowrap; }
.ss-no-data { @apply p-2 text-center; }

/* Queue Backoff Card styles */
.ss-backoff-card {
  @apply mt-4 p-3 rounded-lg border border-amber-200/40 dark:border-amber-800/30;
  @apply bg-gradient-to-br from-amber-50/30 to-orange-50/20 dark:from-amber-900/10 dark:to-orange-900/5;
}
.ss-backoff-header { @apply flex items-center justify-between mb-3 pb-2 border-b border-amber-200/40 dark:border-amber-700/30; }
.ss-backoff-title { @apply text-xs font-semibold text-gray-700 dark:text-gray-300; }
.ss-backoff-summary { @apply flex items-center gap-3 text-[10px]; }
.ss-backoff-total { @apply font-semibold text-amber-700 dark:text-amber-400; }
.ss-backoff-servers { @apply text-gray-500 dark:text-gray-400; }

.ss-servers-grid { @apply grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-3; }
.ss-server-card { @apply p-2 rounded-lg bg-white/60 dark:bg-gray-800/40 border border-gray-200/40 dark:border-gray-700/30; }
.ss-server-header { @apply flex items-center justify-between mb-2 pb-1 border-b border-gray-200/40 dark:border-gray-700/30; }
.ss-server-name { @apply text-[10px] font-semibold text-gray-700 dark:text-gray-300 uppercase tracking-wide; }
.ss-server-badge { @apply text-[9px] font-semibold px-1.5 py-0.5 rounded; }
.ss-badge-ok { @apply bg-emerald-100 text-emerald-700 dark:bg-emerald-900/30 dark:text-emerald-400; }
.ss-badge-warn { @apply bg-amber-100 text-amber-700 dark:bg-amber-900/30 dark:text-amber-400; }

.ss-server-queues { @apply space-y-1 max-h-28 overflow-y-auto; }
.ss-server-empty { @apply text-[10px] text-gray-400 text-center py-2; }
.ss-queue-row { @apply flex items-center justify-between py-1 px-1.5 rounded text-[10px]; }
.ss-queue-row.ss-queue-ok { @apply bg-emerald-50/30 dark:bg-emerald-900/5; }
.ss-queue-row.ss-queue-warn { @apply bg-amber-50/50 dark:bg-amber-900/10; }
.ss-queue-label { @apply text-gray-600 dark:text-gray-400 truncate max-w-[100px]; }
.ss-queue-metrics { @apply flex items-center gap-2 font-mono text-[9px]; }
.ss-metric { @apply whitespace-nowrap; }
.ss-metric-interval { @apply text-gray-400 dark:text-gray-500; }
</style>
