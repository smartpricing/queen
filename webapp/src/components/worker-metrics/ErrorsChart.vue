<template>
  <div class="chart-wrapper">
    <div class="flex items-center gap-2 flex-wrap mb-3">
      <button
        v-for="metric in metrics"
        :key="metric.key"
        @click="toggleMetric(metric.key)"
        :class="['metric-toggle', selectedMetrics[metric.key] ? metric.activeClass : 'metric-toggle-inactive']"
      >
        <div :class="['metric-dot', selectedMetrics[metric.key] ? metric.dotClass : 'bg-gray-400']"></div>
        {{ metric.label }}
      </button>
    </div>
    <div class="chart-container h-48">
      <Bar v-if="chartData" :data="chartData" :options="chartOptions" />
      <div v-else class="flex items-center justify-center h-full text-gray-500 dark:text-gray-400 text-sm">
        No data available
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue';
import { Bar } from 'vue-chartjs';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  BarElement,
  Title,
  Tooltip,
  Legend,
} from 'chart.js';

ChartJS.register(CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend);

const props = defineProps({
  data: Object,
});

const metrics = [
  { key: 'dbErrors', label: 'DB Errors', activeClass: 'metric-toggle-red', dotClass: 'bg-red-500' },
  { key: 'ackFailed', label: 'Ack Failed', activeClass: 'metric-toggle-orange', dotClass: 'bg-orange-500' },
  { key: 'dlq', label: 'DLQ', activeClass: 'metric-toggle-amber', dotClass: 'bg-amber-500' },
];

const selectedMetrics = ref({ dbErrors: true, ackFailed: true, dlq: true });

function toggleMetric(key) {
  selectedMetrics.value[key] = !selectedMetrics.value[key];
}

const chartData = computed(() => {
  if (!props.data?.timeSeries?.length) return null;
  const ts = [...props.data.timeSeries].reverse();
  const datasets = [];

  if (selectedMetrics.value.dbErrors) {
    datasets.push({
      label: 'DB Errors',
      data: ts.map(t => t.dbErrors || 0),
      backgroundColor: 'rgba(239, 68, 68, 0.7)',
      borderColor: 'rgba(239, 68, 68, 1)',
      borderWidth: 1,
      borderRadius: 2,
    });
  }

  if (selectedMetrics.value.ackFailed) {
    datasets.push({
      label: 'Ack Failed',
      data: ts.map(t => t.ackFailed || 0),
      backgroundColor: 'rgba(255, 107, 0, 0.7)',
      borderColor: 'rgba(255, 107, 0, 1)',
      borderWidth: 1,
      borderRadius: 2,
    });
  }

  if (selectedMetrics.value.dlq) {
    datasets.push({
      label: 'DLQ',
      data: ts.map(t => t.dlqCount || 0),
      backgroundColor: 'rgba(245, 158, 11, 0.7)',
      borderColor: 'rgba(245, 158, 11, 1)',
      borderWidth: 1,
      borderRadius: 2,
    });
  }

  return {
    labels: ts.map(t => formatTime(t.timestamp)),
    datasets,
  };
});

function formatTime(ts) {
  return new Date(ts).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
}

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: { mode: 'index', intersect: false },
  scales: {
    x: {
      stacked: true,
      grid: { display: false },
      border: { display: false },
      ticks: { maxRotation: 0, autoSkipPadding: 30, font: { size: 10 }, color: '#6b7280' },
    },
    y: {
      stacked: true,
      beginAtZero: true,
      border: { display: false },
      grid: { color: 'rgba(156, 163, 175, 0.1)' },
      ticks: { font: { size: 10 }, color: '#6b7280' },
    },
  },
  plugins: {
    legend: { display: false },
    tooltip: {
      backgroundColor: 'rgba(17, 24, 39, 0.95)',
      padding: 10,
      cornerRadius: 6,
    },
  },
};
</script>

<style scoped>
.metric-toggle {
  display: flex;
  align-items: center;
  gap: 0.375rem;
  padding: 0.25rem 0.5rem;
  border-radius: 0.375rem;
  font-size: 0.625rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}
.metric-toggle-inactive {
  background: transparent;
  color: #6b7280;
  border: 1px solid rgba(0, 0, 0, 0.1);
}
.dark .metric-toggle-inactive {
  border-color: rgba(255, 255, 255, 0.1);
  color: #9ca3af;
}
.metric-toggle-red {
  background: rgba(239, 68, 68, 0.12);
  color: #dc2626;
  border: 1px solid rgba(239, 68, 68, 0.3);
}
.metric-toggle-orange {
  background: rgba(255, 107, 0, 0.12);
  color: #ea580c;
  border: 1px solid rgba(255, 107, 0, 0.3);
}
.metric-toggle-amber {
  background: rgba(245, 158, 11, 0.12);
  color: #d97706;
  border: 1px solid rgba(245, 158, 11, 0.3);
}
.metric-dot {
  width: 0.375rem;
  height: 0.375rem;
  border-radius: 50%;
}
</style>

