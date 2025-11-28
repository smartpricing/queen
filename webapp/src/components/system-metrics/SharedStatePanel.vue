<template>
  <div class="shared-state-panel" v-if="stats?.enabled">
    <!-- Header -->
    <div class="ss-header">
      <div class="flex items-center gap-2">
        <svg class="w-4 h-4 text-violet-500" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="2">
          <path stroke-linecap="round" stroke-linejoin="round" d="M7.5 21L3 16.5m0 0L7.5 12M3 16.5h13.5m0-13.5L21 7.5m0 0L16.5 12M21 7.5H7.5" />
        </svg>
        <span class="ss-title">Distributed Shared State</span>
        <span class="ss-badge">Active</span>
      </div>
    </div>
    
    <!-- Main Content: Stats Cards + Chart -->
    <div class="ss-content">
      <!-- Left: Cache Stats Cards -->
      <div class="ss-cards-grid">
        <!-- Queue Configs -->
        <div class="ss-card ss-card-blue">
          <div class="ss-card-top">
            <span class="ss-card-title">Queue Configs</span>
            <span :class="['ss-hit-rate', getHitRateClass(stats.queue_config_cache?.hit_rate)]">
              {{ formatPercent(stats.queue_config_cache?.hit_rate) }}
            </span>
          </div>
          <div class="ss-card-stats">
            <div class="ss-stat"><span class="ss-stat-value">{{ stats.queue_config_cache?.size || 0 }}</span><span class="ss-stat-label">cached</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ formatK(stats.queue_config_cache?.hits) }}</span><span class="ss-stat-label">hits</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ formatK(stats.queue_config_cache?.misses) }}</span><span class="ss-stat-label">miss</span></div>
          </div>
        </div>
        
        <!-- Partition IDs -->
        <div class="ss-card ss-card-amber">
          <div class="ss-card-top">
            <span class="ss-card-title">Partition IDs</span>
            <span :class="['ss-hit-rate', getHitRateClass(stats.partition_id_cache?.hit_rate)]">
              {{ formatPercent(stats.partition_id_cache?.hit_rate) }}
            </span>
          </div>
          <div class="ss-card-stats">
            <div class="ss-stat"><span class="ss-stat-value">{{ stats.partition_id_cache?.size || 0 }}</span><span class="ss-stat-label">cached</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ formatK(stats.partition_id_cache?.hits) }}</span><span class="ss-stat-label">hits</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ stats.partition_id_cache?.evictions || 0 }}</span><span class="ss-stat-label">evict</span></div>
          </div>
        </div>
        
        <!-- Lease Hints -->
        <div class="ss-card ss-card-purple">
          <div class="ss-card-top">
            <span class="ss-card-title">Lease Hints</span>
            <span :class="['ss-hit-rate', getHitRateClass(stats.lease_hints?.accuracy)]">
              {{ formatPercent(stats.lease_hints?.accuracy) }}
            </span>
          </div>
          <div class="ss-card-stats">
            <div class="ss-stat"><span class="ss-stat-value">{{ stats.lease_hints?.size || 0 }}</span><span class="ss-stat-label">cached</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ formatK(stats.lease_hints?.hints_used) }}</span><span class="ss-stat-label">used</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ stats.lease_hints?.hints_wrong || 0 }}</span><span class="ss-stat-label">wrong</span></div>
          </div>
        </div>
        
        <!-- Consumer Presence -->
        <div class="ss-card ss-card-green">
          <div class="ss-card-top">
            <span class="ss-card-title">Consumers</span>
          </div>
          <div class="ss-card-stats">
            <div class="ss-stat"><span class="ss-stat-value">{{ stats.consumer_presence?.queues_tracked || 0 }}</span><span class="ss-stat-label">queues</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ stats.consumer_presence?.servers_tracked || 0 }}</span><span class="ss-stat-label">servers</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ formatK(stats.consumer_presence?.total_registrations) }}</span><span class="ss-stat-label">regs</span></div>
          </div>
        </div>
        
        <!-- Server Health -->
        <div class="ss-card ss-card-gray">
          <div class="ss-card-top">
            <span class="ss-card-title">Health</span>
          </div>
          <div class="ss-card-stats">
            <div class="ss-stat"><span class="ss-stat-value text-emerald-500">{{ stats.server_health?.servers_alive || 0 }}</span><span class="ss-stat-label">alive</span></div>
            <div class="ss-stat"><span class="ss-stat-value text-red-500">{{ stats.server_health?.servers_dead || 0 }}</span><span class="ss-stat-label">dead</span></div>
          </div>
        </div>
        
        <!-- Transport -->
        <div class="ss-card ss-card-gray">
          <div class="ss-card-top">
            <span class="ss-card-title">UDP</span>
          </div>
          <div class="ss-card-stats">
            <div class="ss-stat"><span class="ss-stat-value">{{ formatK(stats.transport?.messages_sent) }}</span><span class="ss-stat-label">sent</span></div>
            <div class="ss-stat"><span class="ss-stat-value">{{ formatK(stats.transport?.messages_received) }}</span><span class="ss-stat-label">recv</span></div>
          </div>
        </div>
      </div>
      
      <!-- Right: Chart -->
      <div class="ss-chart-section" v-if="chartData">
        <div class="ss-chart-header">
          <span class="ss-chart-title">Cache Hit Rate History</span>
          <div class="ss-chart-legend">
            <span class="legend-item"><span class="legend-dot bg-blue-500"></span>Queue Config</span>
            <span class="legend-item"><span class="legend-dot bg-amber-500"></span>Partition ID</span>
            <span class="legend-item"><span class="legend-dot bg-purple-500"></span>Lease Hints</span>
          </div>
        </div>
        <div class="ss-chart-container">
          <Line :data="chartData" :options="chartOptions" />
        </div>
      </div>
    </div>
  </div>
  
  <!-- Disabled State -->
  <div v-else-if="!loading && (!stats || !stats.enabled)" class="ss-disabled">
    <svg class="w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M18.364 18.364A9 9 0 005.636 5.636m12.728 12.728A9 9 0 015.636 5.636m12.728 12.728L5.636 5.636" />
    </svg>
    <span class="text-xs text-gray-500 dark:text-gray-400">Shared State disabled</span>
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

const stats = computed(() => {
  if (!props.data) return null;
  
  const ss = props.data;
  if (!ss || !ss.enabled) {
    return { enabled: false };
  }
  
  const calcHitRate = (hits, misses) => {
    const total = (hits || 0) + (misses || 0);
    return total > 0 ? hits / total : 1.0;
  };
  
  return {
    enabled: true,
    queue_config_cache: {
      size: ss.queue_config_cache?.size || 0,
      hits: ss.queue_config_cache?.hits || 0,
      misses: ss.queue_config_cache?.misses || 0,
      hit_rate: calcHitRate(ss.queue_config_cache?.hits, ss.queue_config_cache?.misses)
    },
    consumer_presence: {
      queues_tracked: ss.consumer_presence?.queues_tracked || 0,
      servers_tracked: ss.consumer_presence?.servers_tracked || 0,
      total_registrations: ss.consumer_presence?.total_registrations || 0
    },
    partition_id_cache: {
      size: ss.partition_id_cache?.size || 0,
      hits: ss.partition_id_cache?.hits || 0,
      misses: ss.partition_id_cache?.misses || 0,
      evictions: ss.partition_id_cache?.evictions || 0,
      hit_rate: calcHitRate(ss.partition_id_cache?.hits, ss.partition_id_cache?.misses)
    },
    lease_hints: {
      size: ss.lease_hints?.size || 0,
      hints_used: ss.lease_hints?.used || 0,
      hints_wrong: ss.lease_hints?.wrong || 0,
      accuracy: calcHitRate(ss.lease_hints?.used, ss.lease_hints?.wrong)
    },
    server_health: {
      servers_alive: ss.server_health?.alive || 0,
      servers_dead: ss.server_health?.dead || 0
    },
    transport: {
      messages_sent: ss.transport?.sent || 0,
      messages_received: ss.transport?.received || 0,
      messages_dropped: ss.transport?.dropped || 0
    }
  };
});

// Build chart data from time series
const chartData = computed(() => {
  if (!props.timeSeriesData?.replicas?.length) return null;
  
  const replicas = props.timeSeriesData.replicas;
  
  // Get all unique timestamps
  const allTimestamps = new Set();
  replicas.forEach(replica => {
    replica.timeSeries?.forEach(point => {
      if (point.metrics?.shared_state?.enabled) {
        allTimestamps.add(point.timestamp);
      }
    });
  });
  
  if (allTimestamps.size === 0) return null;
  
  const sortedTimestamps = Array.from(allTimestamps).sort();
  
  const calcHitRate = (hits, misses) => {
    const total = (hits || 0) + (misses || 0);
    return total > 0 ? (hits / total) * 100 : null;
  };
  
  const queueConfigHits = [];
  const partitionIdHits = [];
  const leaseHintsAccuracy = [];
  
  sortedTimestamps.forEach(ts => {
    let qcHits = 0, qcMisses = 0;
    let pidHits = 0, pidMisses = 0;
    let lhUsed = 0, lhWrong = 0;
    
    replicas.forEach(replica => {
      const point = replica.timeSeries?.find(p => p.timestamp === ts);
      if (point?.metrics?.shared_state?.enabled) {
        const ss = point.metrics.shared_state;
        qcHits += ss.queue_config_cache?.hits || 0;
        qcMisses += ss.queue_config_cache?.misses || 0;
        pidHits += ss.partition_id_cache?.hits || 0;
        pidMisses += ss.partition_id_cache?.misses || 0;
        lhUsed += ss.lease_hints?.used || 0;
        lhWrong += ss.lease_hints?.wrong || 0;
      }
    });
    
    queueConfigHits.push(calcHitRate(qcHits, qcMisses));
    partitionIdHits.push(calcHitRate(pidHits, pidMisses));
    leaseHintsAccuracy.push(calcHitRate(lhUsed, lhWrong));
  });
  
  return {
    labels: sortedTimestamps.map(ts => formatTimestamp(ts)),
    datasets: [
      {
        label: 'Queue Config',
        data: queueConfigHits,
        borderColor: 'rgba(59, 130, 246, 1)',
        backgroundColor: 'rgba(59, 130, 246, 0.08)',
        borderWidth: 2,
        fill: true,
        tension: 0.3,
        pointRadius: 0,
        pointHoverRadius: 4,
      },
      {
        label: 'Partition ID',
        data: partitionIdHits,
        borderColor: 'rgba(245, 158, 11, 1)',
        backgroundColor: 'rgba(245, 158, 11, 0.08)',
        borderWidth: 2,
        fill: true,
        tension: 0.3,
        pointRadius: 0,
        pointHoverRadius: 4,
      },
      {
        label: 'Lease Hints',
        data: leaseHintsAccuracy,
        borderColor: 'rgba(168, 85, 247, 1)',
        backgroundColor: 'rgba(168, 85, 247, 0.08)',
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
  interaction: {
    mode: 'index',
    intersect: false,
  },
  plugins: {
    legend: {
      display: false,
    },
    tooltip: {
      backgroundColor: 'rgba(0, 0, 0, 0.9)',
      padding: 10,
      titleFont: { size: 11 },
      bodyFont: { size: 11 },
      titleColor: '#fff',
      bodyColor: '#fff',
      borderColor: 'rgba(255, 255, 255, 0.1)',
      borderWidth: 1,
      displayColors: true,
      boxWidth: 8,
      boxHeight: 8,
      callbacks: {
        label: function(context) {
          const value = context.parsed.y;
          if (value === null) return `${context.dataset.label}: N/A`;
          return `${context.dataset.label}: ${value.toFixed(1)}%`;
        }
      }
    },
  },
  scales: {
    x: {
      grid: { display: false },
      ticks: {
        color: '#9ca3af',
        font: { size: 10 },
        maxRotation: 0,
        autoSkipPadding: 30,
      },
    },
    y: {
      min: 0,
      max: 100,
      grid: {
        color: 'rgba(0, 0, 0, 0.04)',
        drawBorder: false,
      },
      ticks: {
        color: '#9ca3af',
        font: { size: 10 },
        callback: (value) => value + '%',
        stepSize: 25,
      },
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
  return value.toString();
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

.ss-title {
  @apply text-xs font-semibold text-gray-700 dark:text-gray-300;
}

.ss-badge {
  @apply px-1.5 py-0.5 rounded text-[10px] font-semibold;
  @apply bg-emerald-100 text-emerald-700 dark:bg-emerald-900/30 dark:text-emerald-400;
}

.ss-content {
  @apply grid grid-cols-1 lg:grid-cols-2 gap-4;
}

.ss-cards-grid {
  @apply grid grid-cols-2 md:grid-cols-3 gap-2;
}

.ss-card {
  @apply p-2 rounded-md border;
}

.ss-card-blue {
  @apply bg-blue-50/50 border-blue-200/40 dark:bg-blue-900/10 dark:border-blue-800/30;
}

.ss-card-green {
  @apply bg-emerald-50/50 border-emerald-200/40 dark:bg-emerald-900/10 dark:border-emerald-800/30;
}

.ss-card-amber {
  @apply bg-amber-50/50 border-amber-200/40 dark:bg-amber-900/10 dark:border-amber-800/30;
}

.ss-card-purple {
  @apply bg-purple-50/50 border-purple-200/40 dark:bg-purple-900/10 dark:border-purple-800/30;
}

.ss-card-gray {
  @apply bg-gray-50/50 border-gray-200/40 dark:bg-gray-800/20 dark:border-gray-700/30;
}

.ss-card-top {
  @apply flex items-center justify-between mb-1;
}

.ss-card-title {
  @apply text-[10px] font-semibold text-gray-600 dark:text-gray-400 uppercase tracking-wide;
}

.ss-hit-rate {
  @apply text-xs font-bold;
}

.ss-card-stats {
  @apply flex justify-between;
}

.ss-stat {
  @apply flex flex-col items-center text-center;
}

.ss-stat-value {
  @apply text-sm font-bold text-gray-900 dark:text-gray-100 font-mono leading-tight;
}

.ss-stat-label {
  @apply text-[8px] text-gray-500 dark:text-gray-500 uppercase;
}

.ss-chart-section {
  @apply flex flex-col;
}

.ss-chart-header {
  @apply flex items-center justify-between mb-2;
}

.ss-chart-title {
  @apply text-[10px] font-semibold text-gray-600 dark:text-gray-400 uppercase tracking-wide;
}

.ss-chart-legend {
  @apply flex items-center gap-3;
}

.legend-item {
  @apply flex items-center gap-1 text-[10px] text-gray-500 dark:text-gray-400;
}

.legend-dot {
  @apply w-2 h-2 rounded-full;
}

.ss-chart-container {
  @apply flex-1;
  min-height: 160px;
}

.ss-disabled {
  @apply flex items-center gap-2 p-2 bg-gray-50 dark:bg-gray-800/30 rounded-lg;
}
</style>
