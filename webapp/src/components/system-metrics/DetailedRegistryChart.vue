<template>
  <div class="registry-chart">
    <!-- Metric Toggles -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
      <button
        @click="selectedMetrics.pollIntention = !selectedMetrics.pollIntention"
        :class="[
          'metric-toggle',
          selectedMetrics.pollIntention ? 'metric-toggle-active-blue' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.pollIntention ? 'bg-orange-500' : 'bg-gray-400']"></div>
        Poll Intentions
      </button>
      <button
        @click="selectedMetrics.streamPollIntention = !selectedMetrics.streamPollIntention"
        :class="[
          'metric-toggle',
          selectedMetrics.streamPollIntention ? 'metric-toggle-active-purple' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.streamPollIntention ? 'bg-purple-500' : 'bg-gray-400']"></div>
        Stream Poll Intentions
      </button>
      <button
        @click="selectedMetrics.response = !selectedMetrics.response"
        :class="[
          'metric-toggle',
          selectedMetrics.response ? 'metric-toggle-active-rose' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.response ? 'bg-rose-500' : 'bg-gray-400']"></div>
        Response Registry
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
  pollIntention: true,
  streamPollIntention: true,
  response: true,
});

function getNestedValue(obj, path) {
  return path.split('.').reduce((acc, part) => acc?.[part], obj);
}

// Colors for different replicas
const replicaColors = [
  { pollIntention: 'rgba(255, 107, 0, 1)', streamPollIntention: 'rgba(168, 85, 247, 1)', response: 'rgba(244, 63, 94, 1)' },
  { pollIntention: 'rgba(147, 197, 253, 1)', streamPollIntention: 'rgba(216, 180, 254, 1)', response: 'rgba(251, 113, 133, 1)' },
  { pollIntention: 'rgba(96, 165, 250, 1)', streamPollIntention: 'rgba(192, 132, 252, 1)', response: 'rgba(248, 113, 113, 1)' },
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

    if (selectedMetrics.value.pollIntention) {
      const dataMap = new Map();
      replica.timeSeries.forEach(point => {
        const value = getNestedValue(point.metrics, 'registries.poll_intention');
        dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
      });

      datasets.push({
        label: `${replicaLabel} - Poll Intentions`,
        data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
        borderColor: colorScheme.pollIntention,
        backgroundColor: 'transparent',
        borderWidth: 2,
        fill: false,
        tension: 0,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: colorScheme.pollIntention,
      });
    }

    if (selectedMetrics.value.streamPollIntention) {
      const dataMap = new Map();
      replica.timeSeries.forEach(point => {
        const value = getNestedValue(point.metrics, 'registries.stream_poll_intention');
        dataMap.set(point.timestamp, value?.[props.aggregation] ?? null);
      });

      datasets.push({
        label: `${replicaLabel} - Stream Poll Intentions`,
        data: sortedTimestamps.map(ts => dataMap.get(ts) ?? null),
        borderColor: colorScheme.streamPollIntention,
        backgroundColor: 'transparent',
        borderWidth: 2,
        fill: false,
        tension: 0,
        pointRadius: 0,
        pointHoverRadius: 5,
        pointHoverBackgroundColor: colorScheme.streamPollIntention,
      });
    }

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

.metric-toggle-active-blue {
  background: rgba(255, 107, 0, 0.1);
  color: #FF6B00;
  border: 1px solid rgba(255, 107, 0, 0.2);
}

.metric-toggle-active-purple {
  background: rgba(168, 85, 247, 0.1);
  color: #a855f7;
  border: 1px solid rgba(168, 85, 247, 0.2);
}

.metric-toggle-active-rose {
  background: rgba(244, 63, 94, 0.1);
  color: #f43f5e;
  border: 1px solid rgba(244, 63, 94, 0.2);
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

.metric-toggle-active-blue:hover,
.metric-toggle-active-purple:hover,
.metric-toggle-active-rose:hover {
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

