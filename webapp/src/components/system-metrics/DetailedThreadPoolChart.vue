<template>
  <div class="threadpool-chart">
    <!-- Metric Toggles -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
      <button
        @click="selectedMetrics.dbPoolSize = !selectedMetrics.dbPoolSize"
        :class="[
          'metric-toggle',
          selectedMetrics.dbPoolSize ? 'metric-toggle-active-gray' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.dbPoolSize ? 'bg-gray-500' : 'bg-gray-400']"></div>
        DB Pool Size
      </button>
      <button
        @click="selectedMetrics.dbQueueSize = !selectedMetrics.dbQueueSize"
        :class="[
          'metric-toggle',
          selectedMetrics.dbQueueSize ? 'metric-toggle-active-amber' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.dbQueueSize ? 'bg-amber-500' : 'bg-gray-400']"></div>
        DB Queue Size
      </button>
      <button
        @click="selectedMetrics.systemPoolSize = !selectedMetrics.systemPoolSize"
        :class="[
          'metric-toggle',
          selectedMetrics.systemPoolSize ? 'metric-toggle-active-blue' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.systemPoolSize ? 'bg-blue-500' : 'bg-gray-400']"></div>
        System Pool Size
      </button>
      <button
        @click="selectedMetrics.systemQueueSize = !selectedMetrics.systemQueueSize"
        :class="[
          'metric-toggle',
          selectedMetrics.systemQueueSize ? 'metric-toggle-active-green' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.systemQueueSize ? 'bg-green-500' : 'bg-gray-400']"></div>
        System Queue Size
      </button>
    </div>

    <!-- Chart -->
    <div class="chart-container">
      <Line v-if="chartData" :data="chartData" :options="chartOptions" />
      <div v-else class="flex items-center justify-center h-full text-gray-500 text-sm">
        No data available
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue';
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
  data: Object,
  aggregation: {
    type: String,
    default: 'avg',
  },
});

const selectedMetrics = ref({
  dbPoolSize: false,
  dbQueueSize: true,
  systemPoolSize: false,
  systemQueueSize: true,
});

function getNestedValue(obj, path) {
  return path.split('.').reduce((acc, part) => acc?.[part], obj);
}

// Colors for different replicas
const replicaColors = [
  { dbPool: 'rgba(107, 114, 128, 1)', dbQueue: 'rgba(245, 158, 11, 1)', sysPool: 'rgba(59, 130, 246, 1)', sysQueue: 'rgba(34, 197, 94, 1)' },
  { dbPool: 'rgba(156, 163, 175, 1)', dbQueue: 'rgba(251, 191, 36, 1)', sysPool: 'rgba(147, 197, 253, 1)', sysQueue: 'rgba(134, 239, 172, 1)' },
  { dbPool: 'rgba(75, 85, 99, 1)', dbQueue: 'rgba(217, 119, 6, 1)', sysPool: 'rgba(96, 165, 250, 1)', sysQueue: 'rgba(74, 222, 128, 1)' },
];

const chartData = computed(() => {
  if (!props.data?.replicas?.length) return null;

  const replicas = props.data.replicas;
  const datasets = [];

  // Get all unique timestamps
  const allTimestamps = new Set();
  replicas.forEach(replica => {
    replica.timeSeries.forEach(point => {
      allTimestamps.add(point.timestamp);
    });
  });
  const sortedTimestamps = Array.from(allTimestamps).sort();

  // Determine if we need to show dates (time range > 24 hours)
  const showDates = isMultiDay();

  replicas.forEach((replica, replicaIndex) => {
    const colorScheme = replicaColors[replicaIndex % replicaColors.length];
    const replicaLabel = `${replica.hostname}`;

    if (selectedMetrics.value.dbPoolSize) {
      const dataMap = new Map();
      replica.timeSeries.forEach(point => {
        const value = getNestedValue(point.metrics, 'threadpool.db.pool_size');
        dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
      });

      datasets.push({
        label: `${replicaLabel} - DB Pool Size`,
        data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
        borderColor: colorScheme.dbPool,
        backgroundColor: 'transparent',
        borderWidth: 2,
        fill: false,
        tension: 0,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: colorScheme.dbPool,
      });
    }

    if (selectedMetrics.value.dbQueueSize) {
      const dataMap = new Map();
      replica.timeSeries.forEach(point => {
        const value = getNestedValue(point.metrics, 'threadpool.db.queue_size');
        dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
      });

      datasets.push({
        label: `${replicaLabel} - DB Queue`,
        data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
        borderColor: colorScheme.dbQueue,
        backgroundColor: 'transparent',
        borderWidth: 2,
        fill: false,
        tension: 0,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: colorScheme.dbQueue,
      });
    }

    if (selectedMetrics.value.systemPoolSize) {
      const dataMap = new Map();
      replica.timeSeries.forEach(point => {
        const value = getNestedValue(point.metrics, 'threadpool.system.pool_size');
        dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
      });

      datasets.push({
        label: `${replicaLabel} - System Pool`,
        data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
        borderColor: colorScheme.sysPool,
        backgroundColor: 'transparent',
        borderWidth: 2,
        fill: false,
        tension: 0,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: colorScheme.sysPool,
      });
    }

    if (selectedMetrics.value.systemQueueSize) {
      const dataMap = new Map();
      replica.timeSeries.forEach(point => {
        const value = getNestedValue(point.metrics, 'threadpool.system.queue_size');
        dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
      });

      datasets.push({
        label: `${replicaLabel} - System Queue`,
        data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
        borderColor: colorScheme.sysQueue,
        backgroundColor: 'transparent',
        borderWidth: 2,
        fill: false,
        tension: 0,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: colorScheme.sysQueue,
      });
    }
  });

  return {
    labels: sortedTimestamps.map(ts => formatTimestamp(ts, showDates)),
    datasets,
  };
});

function isMultiDay() {
  if (!props.data?.timeRange?.from || !props.data?.timeRange?.to) return false;
  const from = new Date(props.data.timeRange.from);
  const to = new Date(props.data.timeRange.to);
  const diffHours = (to - from) / (1000 * 60 * 60);
  return diffHours > 24;
}

function formatTimestamp(ts, showDate) {
  const date = new Date(ts);
  if (showDate) {
    return date.toLocaleString('en-US', { 
      month: 'short', 
      day: 'numeric',
      hour: '2-digit', 
      minute: '2-digit' 
    });
  } else {
    return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
  }
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
      display: true,
      position: 'bottom',
      labels: {
        boxWidth: 12,
        boxHeight: 12,
        padding: 10,
        font: {
          size: 11,
        },
        color: '#9ca3af',
        usePointStyle: true,
      },
    },
    tooltip: {
      backgroundColor: 'rgba(0, 0, 0, 0.9)',
      padding: 12,
      titleColor: '#fff',
      bodyColor: '#fff',
      borderColor: 'rgba(255, 255, 255, 0.1)',
      borderWidth: 1,
      displayColors: true,
      callbacks: {
        title: function(context) {
          if (context[0]?.label) {
            return context[0].label;
          }
          return '';
        },
        label: function(context) {
          return `${context.dataset.label}: ${Math.round(context.parsed.y)}`;
        }
      }
    },
  },
  scales: {
    x: {
      grid: {
        display: false,
      },
      ticks: {
        color: '#9ca3af',
        font: {
          size: 11,
        },
        maxRotation: 45,
        minRotation: 0,
        autoSkipPadding: 30,
      },
    },
    y: {
      type: 'linear',
      display: true,
      position: 'left',
      beginAtZero: true,
      grid: {
        color: 'rgba(0, 0, 0, 0.05)',
        drawBorder: false,
      },
      ticks: {
        color: '#9ca3af',
        font: {
          size: 11,
        },
        callback: function(value) {
          return Math.round(value);
        },
      },
      title: {
        display: true,
        text: 'Count',
        color: '#6b7280',
        font: {
          size: 12,
          weight: 600,
        },
      },
    },
  },
};
</script>

<style scoped>
.threadpool-chart {
  width: 100%;
}

.chart-container {
  height: 350px;
  position: relative;
}

.metric-toggle {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.375rem 0.75rem;
  border-radius: 0.5rem;
  font-size: 0.75rem;
  font-weight: 500;
  transition: all 0.2s ease;
  cursor: pointer;
}

.metric-toggle-active-gray {
  background: rgba(107, 114, 128, 0.1);
  color: #6b7280;
  border: 1px solid rgba(107, 114, 128, 0.2);
}

.metric-toggle-active-amber {
  background: rgba(245, 158, 11, 0.1);
  color: #f59e0b;
  border: 1px solid rgba(245, 158, 11, 0.2);
}

.metric-toggle-active-blue {
  background: rgba(59, 130, 246, 0.1);
  color: #3b82f6;
  border: 1px solid rgba(59, 130, 246, 0.2);
}

.metric-toggle-active-green {
  background: rgba(34, 197, 94, 0.1);
  color: #22c55e;
  border: 1px solid rgba(34, 197, 94, 0.2);
}

.metric-toggle-inactive {
  background: transparent;
  color: #9ca3af;
  border: 1px solid rgba(0, 0, 0, 0.1);
}

.dark .metric-toggle-inactive {
  border-color: rgba(255, 255, 255, 0.1);
}

.metric-toggle:hover {
  transform: translateY(-1px);
}

.metric-toggle-active-gray:hover,
.metric-toggle-active-amber:hover,
.metric-toggle-active-blue:hover,
.metric-toggle-active-green:hover {
  opacity: 0.9;
}

.metric-toggle-inactive:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .metric-toggle-inactive:hover {
  background: rgba(255, 255, 255, 0.05);
}

.metric-dot {
  width: 0.5rem;
  height: 0.5rem;
  border-radius: 50%;
}
</style>

