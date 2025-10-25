<template>
  <div class="database-chart">
    <!-- Metric Toggles -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
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
        <div :class="['metric-dot', selectedMetrics.poolActive ? 'bg-blue-500' : 'bg-gray-400']"></div>
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
});

const selectedMetrics = ref({
  poolSize: true,
  poolActive: true,
  poolIdle: false,
});

function getNestedValue(obj, path) {
  return path.split('.').reduce((acc, part) => acc?.[part], obj);
}

const chartData = computed(() => {
  if (!props.data?.timeSeries?.length) return null;

  const timeSeries = props.data.timeSeries;
  const datasets = [];

  if (selectedMetrics.value.poolSize) {
    datasets.push({
      label: 'Pool Size',
      data: timeSeries.map(point => {
        const value = getNestedValue(point.metrics, 'database.pool_size');
        return value?.[props.aggregation] !== undefined ? value[props.aggregation] : null;
      }),
      borderColor: 'rgba(107, 114, 128, 1)',
      backgroundColor: 'rgba(107, 114, 128, 0.1)',
      borderWidth: 2,
      fill: false,
      tension: 0,
      pointRadius: 2,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(107, 114, 128, 1)',
    });
  }

  if (selectedMetrics.value.poolActive) {
    datasets.push({
      label: 'Active',
      data: timeSeries.map(point => {
        const value = getNestedValue(point.metrics, 'database.pool_active');
        return value?.[props.aggregation] !== undefined ? value[props.aggregation] : null;
      }),
      borderColor: 'rgba(59, 130, 246, 1)',
      backgroundColor: 'rgba(59, 130, 246, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 2,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(59, 130, 246, 1)',
    });
  }

  if (selectedMetrics.value.poolIdle) {
    datasets.push({
      label: 'Idle',
      data: timeSeries.map(point => {
        const value = getNestedValue(point.metrics, 'database.pool_idle');
        return value?.[props.aggregation] !== undefined ? value[props.aggregation] : null;
      }),
      borderColor: 'rgba(34, 197, 94, 1)',
      backgroundColor: 'rgba(34, 197, 94, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 2,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(34, 197, 94, 1)',
    });
  }

  return {
    labels: timeSeries.map(point => {
      const date = new Date(point.timestamp);
      return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    }),
    datasets,
  };
});

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
      padding: 12,
      titleColor: '#fff',
      bodyColor: '#fff',
      borderColor: 'rgba(255, 255, 255, 0.1)',
      borderWidth: 1,
      displayColors: true,
      callbacks: {
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
        maxRotation: 0,
        autoSkipPadding: 20,
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

