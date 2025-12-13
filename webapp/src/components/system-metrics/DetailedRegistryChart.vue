<template>
  <div class="registry-chart">
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
  response: true,
});

function getNestedValue(obj, path) {
  return path.split('.').reduce((acc, part) => acc?.[part], obj);
}

// Colors for different replicas
const replicaColors = [
  { response: 'rgba(244, 63, 94, 1)' },
  { response: 'rgba(251, 113, 133, 1)' },
  { response: 'rgba(248, 113, 113, 1)' },
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
      response: 'rgba(244, 63, 94, 1)',
    };

    if (selectedMetrics.value.response) {
      const aggregatedData = sortedTimestamps.map(ts => {
        const values = [];
        replicas.forEach(replica => {
          const point = replica.timeSeries.find(p => p.timestamp === ts);
          if (point) {
            const value = getNestedValue(point.metrics, 'registries.response');
            const rawValue = value?.[props.aggregation];
            if (rawValue !== undefined && rawValue !== null) values.push(rawValue);
          }
        });
        return aggregateValues(values, props.aggregation);
      });

      datasets.push({
        label: `Response Registry (${props.aggregation})`,
        data: aggregatedData,
        borderColor: aggregateColors.response,
        backgroundColor: 'rgba(244, 63, 94, 0.1)',
        borderWidth: 2,
        fill: true,
        tension: 0.2,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: aggregateColors.response,
      });
    }
  } else {
    // Individual mode: one line per replica per metric
    replicas.forEach((replica, replicaIndex) => {
      const colorScheme = replicaColors[replicaIndex % replicaColors.length];
      const replicaLabel = `${replica.hostname}`;

      if (selectedMetrics.value.response) {
        const dataMap = new Map();
        replica.timeSeries.forEach(point => {
          const value = getNestedValue(point.metrics, 'registries.response');
          dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
        });

        datasets.push({
          label: `${replicaLabel} - Response Registry`,
          data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
          borderColor: colorScheme.response,
          backgroundColor: 'transparent',
          borderWidth: 2,
          fill: false,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 5,
          pointHoverBackgroundColor: colorScheme.response,
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
        text: 'Active Entries',
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
.registry-chart {
  width: 100%;
}

.chart-container {
  height: 200px;
  position: relative;
}

</style>

