<template>
  <div class="database-chart">
    <!-- Metric Toggles -->
    <div class="flex items-center gap-1.5 flex-wrap mb-2">
      <button
        @click="selectedMetrics.poolSize = !selectedMetrics.poolSize"
        :class="[
          'metric-toggle',
          selectedMetrics.poolSize ? 'metric-toggle-active-gray' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.poolSize ? 'bg-gray-500' : 'bg-gray-400']"></div>
        Pool Size
      </button>
      <button
        @click="selectedMetrics.poolActive = !selectedMetrics.poolActive"
        :class="[
          'metric-toggle',
          selectedMetrics.poolActive ? 'metric-toggle-active-blue' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.poolActive ? 'bg-orange-500' : 'bg-gray-400']"></div>
        Active Connections
      </button>
      <button
        @click="selectedMetrics.poolIdle = !selectedMetrics.poolIdle"
        :class="[
          'metric-toggle',
          selectedMetrics.poolIdle ? 'metric-toggle-active-green' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.poolIdle ? 'bg-green-500' : 'bg-gray-400']"></div>
        Idle Connections
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
  viewMode: {
    type: String,
    default: 'individual', // 'individual' or 'aggregate'
  },
});

const selectedMetrics = ref({
  poolSize: true,
  poolActive: true,
  poolIdle: false,
});

function getNestedValue(obj, path) {
  return path.split('.').reduce((acc, part) => acc?.[part], obj);
}

// Colors for different replicas
const replicaColors = [
  { poolSize: 'rgba(107, 114, 128, 1)', active: 'rgba(255, 107, 0, 1)', idle: 'rgba(34, 197, 94, 1)' },
  { poolSize: 'rgba(156, 163, 175, 1)', active: 'rgba(147, 197, 253, 1)', idle: 'rgba(134, 239, 172, 1)' },
  { poolSize: 'rgba(75, 85, 99, 1)', active: 'rgba(96, 165, 250, 1)', idle: 'rgba(74, 222, 128, 1)' },
];

// Helper function to aggregate values across replicas
function aggregateValues(values, aggregationType) {
  if (values.length === 0) return null;
  if (aggregationType === 'max') {
    return Math.max(...values);
  } else if (aggregationType === 'min') {
    return Math.min(...values);
  } else {
    return values.reduce((a, b) => a + b, 0) / values.length;
  }
}

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

  if (props.viewMode === 'aggregate') {
    // Aggregate mode: combine all replicas into single lines per metric
    const aggregateColors = {
      poolSize: 'rgba(107, 114, 128, 1)',
      active: 'rgba(255, 107, 0, 1)',
      idle: 'rgba(34, 197, 94, 1)',
    };

    if (selectedMetrics.value.poolSize) {
      const aggregatedData = sortedTimestamps.map(ts => {
        const values = [];
        replicas.forEach(replica => {
          const point = replica.timeSeries.find(p => p.timestamp === ts);
          if (point) {
            const value = getNestedValue(point.metrics, 'database.pool_size');
            const rawValue = value?.[props.aggregation];
            if (rawValue !== undefined && rawValue !== null) values.push(rawValue);
          }
        });
        return aggregateValues(values, props.aggregation);
      });

      datasets.push({
        label: `Pool Size (${props.aggregation})`,
        data: aggregatedData,
        borderColor: aggregateColors.poolSize,
        backgroundColor: 'rgba(107, 114, 128, 0.1)',
        borderWidth: 2,
        fill: true,
        tension: 0.2,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: aggregateColors.poolSize,
      });
    }

    if (selectedMetrics.value.poolActive) {
      const aggregatedData = sortedTimestamps.map(ts => {
        const values = [];
        replicas.forEach(replica => {
          const point = replica.timeSeries.find(p => p.timestamp === ts);
          if (point) {
            const value = getNestedValue(point.metrics, 'database.pool_active');
            const rawValue = value?.[props.aggregation];
            if (rawValue !== undefined && rawValue !== null) values.push(rawValue);
          }
        });
        return aggregateValues(values, props.aggregation);
      });

      datasets.push({
        label: `Active (${props.aggregation})`,
        data: aggregatedData,
        borderColor: aggregateColors.active,
        backgroundColor: 'rgba(255, 107, 0, 0.1)',
        borderWidth: 2,
        fill: true,
        tension: 0.2,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: aggregateColors.active,
      });
    }

    if (selectedMetrics.value.poolIdle) {
      const aggregatedData = sortedTimestamps.map(ts => {
        const values = [];
        replicas.forEach(replica => {
          const point = replica.timeSeries.find(p => p.timestamp === ts);
          if (point) {
            const value = getNestedValue(point.metrics, 'database.pool_idle');
            const rawValue = value?.[props.aggregation];
            if (rawValue !== undefined && rawValue !== null) values.push(rawValue);
          }
        });
        return aggregateValues(values, props.aggregation);
      });

      datasets.push({
        label: `Idle (${props.aggregation})`,
        data: aggregatedData,
        borderColor: aggregateColors.idle,
        backgroundColor: 'rgba(34, 197, 94, 0.1)',
        borderWidth: 2,
        fill: true,
        tension: 0.2,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: aggregateColors.idle,
      });
    }
  } else {
    // Individual mode: one line per replica per metric
    replicas.forEach((replica, replicaIndex) => {
      const colorScheme = replicaColors[replicaIndex % replicaColors.length];
      const replicaLabel = `${replica.hostname}`;

      if (selectedMetrics.value.poolSize) {
        const dataMap = new Map();
        replica.timeSeries.forEach(point => {
          const value = getNestedValue(point.metrics, 'database.pool_size');
          dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
        });

        datasets.push({
          label: `${replicaLabel} - Pool Size`,
          data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
          borderColor: colorScheme.poolSize,
          backgroundColor: 'transparent',
          borderWidth: 2,
          fill: false,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 5,
          pointHoverBackgroundColor: colorScheme.poolSize,
        });
      }

      if (selectedMetrics.value.poolActive) {
        const dataMap = new Map();
        replica.timeSeries.forEach(point => {
          const value = getNestedValue(point.metrics, 'database.pool_active');
          dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
        });

        datasets.push({
          label: `${replicaLabel} - Active`,
          data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
          borderColor: colorScheme.active,
          backgroundColor: 'transparent',
          borderWidth: 2,
          fill: false,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 5,
          pointHoverBackgroundColor: colorScheme.active,
        });
      }

      if (selectedMetrics.value.poolIdle) {
        const dataMap = new Map();
        replica.timeSeries.forEach(point => {
          const value = getNestedValue(point.metrics, 'database.pool_idle');
          dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
        });

        datasets.push({
          label: `${replicaLabel} - Idle`,
          data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
          borderColor: colorScheme.idle,
          backgroundColor: 'transparent',
          borderWidth: 2,
          fill: false,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 5,
          pointHoverBackgroundColor: colorScheme.idle,
        });
      }
    });
  }

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
        text: 'Connections',
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
.database-chart {
  width: 100%;
}

.chart-container {
  height: 200px;
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

.metric-toggle-active-blue {
  background: rgba(255, 107, 0, 0.1);
  color: #FF6B00;
  border: 1px solid rgba(255, 107, 0, 0.2);
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

