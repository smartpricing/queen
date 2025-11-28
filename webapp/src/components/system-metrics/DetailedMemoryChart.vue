<template>
  <div class="memory-chart">
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
} from 'chart.js';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend
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

function getNestedValue(obj, path) {
  return path.split('.').reduce((acc, part) => acc?.[part], obj);
}

// Colors for different replicas (same as CPU for consistency)
const replicaColors = [
  { color: 'rgba(255, 107, 0, 1)', bg: 'rgba(255, 107, 0, 0.1)' },
  { color: 'rgba(255, 107, 0, 1)', bg: 'rgba(255, 107, 0, 0.1)' },
  { color: 'rgba(245, 158, 11, 1)', bg: 'rgba(245, 158, 11, 0.1)' },
  { color: 'rgba(14, 165, 233, 1)', bg: 'rgba(14, 165, 233, 0.1)' },
];

const chartData = computed(() => {
  if (!props.data?.replicas?.length) return null;

  const replicas = props.data.replicas;
  const datasets = [];

  // Get all unique timestamps across all replicas
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
    // Aggregate mode: combine all replicas into a single line
    const aggregatedData = sortedTimestamps.map(ts => {
      const values = [];
      replicas.forEach(replica => {
        const point = replica.timeSeries.find(p => p.timestamp === ts);
        if (point) {
          const value = getNestedValue(point.metrics, 'memory.rss_bytes');
          const rawValue = value?.[props.aggregation];
          if (rawValue !== undefined && rawValue !== null) {
            values.push(rawValue / 1024 / 1024);
          }
        }
      });
      if (values.length === 0) return null;
      // Aggregate based on selected aggregation type
      if (props.aggregation === 'max') {
        return Math.max(...values);
      } else if (props.aggregation === 'min') {
        return Math.min(...values);
      } else {
        return values.reduce((a, b) => a + b, 0) / values.length;
      }
    });

    datasets.push({
      label: `All Servers (${props.aggregation})`,
      data: aggregatedData,
      borderColor: 'rgba(168, 85, 247, 1)',
      backgroundColor: 'rgba(168, 85, 247, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0.2,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(168, 85, 247, 1)',
    });
  } else {
    // Individual mode: one line per replica
    replicas.forEach((replica, replicaIndex) => {
      const colorScheme = replicaColors[replicaIndex % replicaColors.length];
      const replicaLabel = `${replica.hostname}`;

      // Memory MB dataset for this replica
      const dataMap = new Map();
      replica.timeSeries.forEach(point => {
        const value = getNestedValue(point.metrics, 'memory.rss_bytes');
        const rawValue = value?.[props.aggregation] !== undefined ? value[props.aggregation] : null;
        dataMap.set(point.timestamp, rawValue !== null ? (rawValue / 1024 / 1024) : null);
      });

      datasets.push({
        label: replicaLabel,
        data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
        borderColor: colorScheme.color,
        backgroundColor: colorScheme.bg,
        borderWidth: 2,
        fill: false,
        tension: 0,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: colorScheme.color,
      });
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
          const value = context.parsed.y;
          return `${context.dataset.label}: ${value.toFixed(0)} MB`;
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
          return value.toFixed(0) + ' MB';
        },
      },
      title: {
        display: true,
        text: 'Memory (MB)',
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
.memory-chart {
  width: 100%;
}

.chart-container {
  height: 180px;
  position: relative;
}
</style>

