<template>
  <div class="worker-health-chart">
    <!-- Metric Selector -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
      <button
        @click="selectedMetrics.eventLoopLag = !selectedMetrics.eventLoopLag"
        :class="[
          'metric-toggle',
          selectedMetrics.eventLoopLag ? 'metric-toggle-active-red' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.eventLoopLag ? 'bg-red-500' : 'bg-gray-400']"></div>
        Event Loop Lag
      </button>
      <button
        @click="selectedMetrics.messageLag = !selectedMetrics.messageLag"
        :class="[
          'metric-toggle',
          selectedMetrics.messageLag ? 'metric-toggle-active-yellow' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.messageLag ? 'bg-yellow-500' : 'bg-gray-400']"></div>
        Message Lag
      </button>
      <button
        @click="selectedMetrics.freeSlots = !selectedMetrics.freeSlots"
        :class="[
          'metric-toggle',
          selectedMetrics.freeSlots ? 'metric-toggle-active-green' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.freeSlots ? 'bg-green-500' : 'bg-gray-400']"></div>
        Free Slots
      </button>
    </div>

    <!-- Chart -->
    <div class="chart-container h-52">
      <Line v-if="chartData" :data="chartData" :options="chartOptions" />
      <div v-else class="flex items-center justify-center h-full text-gray-500 dark:text-gray-400 text-sm">
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
});

const selectedMetrics = ref({
  eventLoopLag: true,
  messageLag: true,
  freeSlots: false,
});

const chartData = computed(() => {
  if (!props.data?.throughput?.length) return null;

  // Reverse the array since API returns data in descending order
  const throughput = [...props.data.throughput].reverse();
  
  const datasets = [];

  if (selectedMetrics.value.eventLoopLag) {
    datasets.push({
      label: 'Max Event Loop Lag',
      data: throughput.map(t => t.maxEventLoopLagMs || 0),
      borderColor: 'rgba(239, 68, 68, 0.9)',
      backgroundColor: 'rgba(239, 68, 68, 0.1)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
      yAxisID: 'y',
    });
  }

  if (selectedMetrics.value.messageLag) {
    datasets.push({
      label: 'Avg Message Lag',
      data: throughput.map(t => t.avgLagMs || 0),
      borderColor: 'rgba(234, 179, 8, 0.9)',
      backgroundColor: 'rgba(234, 179, 8, 0.1)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
      yAxisID: 'y',
    });
  }

  if (selectedMetrics.value.freeSlots) {
    datasets.push({
      label: 'Min Free Slots',
      data: throughput.map(t => t.minFreeSlots || 0),
      borderColor: 'rgba(34, 197, 94, 0.9)',
      backgroundColor: 'rgba(34, 197, 94, 0.1)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
      yAxisID: 'y2',
    });
  }
  
  return {
    labels: throughput.map(t => formatTimestamp(t.timestamp)),
    datasets,
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
  scales: {
    x: {
      grid: {
        display: false,
      },
      border: {
        display: false,
      },
      ticks: {
        maxRotation: 45,
        minRotation: 0,
        autoSkipPadding: 30,
        font: {
          size: 11,
          family: 'Inter, -apple-system, sans-serif',
        },
        color: '#6b7280',
      },
    },
    y: {
      type: 'linear',
      display: true,
      position: 'left',
      beginAtZero: true,
      title: {
        display: true,
        text: 'Latency (ms)',
        color: '#6b7280',
        font: {
          size: 10,
        },
      },
      border: {
        display: false,
      },
      grid: {
        display: true,
        color: 'rgba(156, 163, 175, 0.1)',
        drawBorder: false,
      },
      ticks: {
        font: {
          size: 11,
          family: 'Inter, -apple-system, sans-serif',
        },
        color: '#6b7280',
        padding: 8,
        callback: (value) => value >= 1000 ? `${(value/1000).toFixed(1)}s` : `${value}ms`,
      },
    },
    y2: {
      type: 'linear',
      display: selectedMetrics.value.freeSlots,
      position: 'right',
      beginAtZero: true,
      title: {
        display: true,
        text: 'Slots',
        color: '#6b7280',
        font: {
          size: 10,
        },
      },
      border: {
        display: false,
      },
      grid: {
        drawOnChartArea: false,
      },
      ticks: {
        font: {
          size: 11,
          family: 'Inter, -apple-system, sans-serif',
        },
        color: '#6b7280',
        padding: 8,
      },
    },
  },
  plugins: {
    legend: {
      display: false,
    },
    tooltip: {
      enabled: true,
      backgroundColor: 'rgba(17, 24, 39, 0.95)',
      padding: 10,
      cornerRadius: 6,
      titleFont: {
        size: 12,
        weight: '600',
        family: 'Inter, -apple-system, sans-serif',
      },
      bodyFont: {
        size: 11,
        family: 'Inter, -apple-system, sans-serif',
      },
      bodySpacing: 4,
      usePointStyle: true,
      callbacks: {
        title: (items) => items[0].label,
        label: (context) => {
          const label = context.dataset.label;
          const value = context.parsed.y;
          if (label.includes('Lag')) {
            return ` ${label}: ${value >= 1000 ? (value/1000).toFixed(2) + 's' : value + 'ms'}`;
          }
          return ` ${label}: ${value}`;
        },
      },
    },
  },
};
</script>

<style scoped>
.worker-health-chart {
  width: 100%;
}

.metric-toggle {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 0.75rem;
  border-radius: 0.5rem;
  font-size: 0.6875rem;
  font-weight: 600;
  transition: all 0.15s cubic-bezier(0.4, 0, 0.2, 1);
  cursor: pointer;
  letter-spacing: 0.01em;
}

.metric-toggle-active-red {
  background: rgba(239, 68, 68, 0.12);
  color: #dc2626;
  border: 1px solid rgba(239, 68, 68, 0.3);
}

.dark .metric-toggle-active-red {
  background: rgba(239, 68, 68, 0.18);
  color: #f87171;
  border: 1px solid rgba(239, 68, 68, 0.4);
}

.metric-toggle-active-yellow {
  background: rgba(234, 179, 8, 0.12);
  color: #ca8a04;
  border: 1px solid rgba(234, 179, 8, 0.3);
}

.dark .metric-toggle-active-yellow {
  background: rgba(234, 179, 8, 0.18);
  color: #facc15;
  border: 1px solid rgba(234, 179, 8, 0.4);
}

.metric-toggle-active-green {
  background: rgba(34, 197, 94, 0.12);
  color: #16a34a;
  border: 1px solid rgba(34, 197, 94, 0.3);
}

.dark .metric-toggle-active-green {
  background: rgba(34, 197, 94, 0.18);
  color: #4ade80;
  border: 1px solid rgba(34, 197, 94, 0.4);
}

.metric-toggle-inactive {
  background: transparent;
  color: #6b7280;
  border: 1px solid rgba(0, 0, 0, 0.12);
}

.dark .metric-toggle-inactive {
  color: #9ca3af;
  border-color: rgba(255, 255, 255, 0.12);
}

.metric-toggle:hover {
  filter: brightness(1.05);
}

.metric-dot {
  width: 0.4rem;
  height: 0.4rem;
  border-radius: 50%;
}

</style>

