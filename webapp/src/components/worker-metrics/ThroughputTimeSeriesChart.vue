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

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend, Filler);

const props = defineProps({
  data: Object,
});

const metrics = [
  { key: 'push', label: 'Push', activeClass: 'metric-toggle-orange', dotClass: 'bg-orange-500' },
  { key: 'pop', label: 'Pop', activeClass: 'metric-toggle-indigo', dotClass: 'bg-indigo-500' },
  { key: 'ack', label: 'Ack', activeClass: 'metric-toggle-green', dotClass: 'bg-green-500' },
];

const selectedMetrics = ref({ push: true, pop: true, ack: true });

function toggleMetric(key) {
  selectedMetrics.value[key] = !selectedMetrics.value[key];
}

const chartData = computed(() => {
  if (!props.data?.timeSeries?.length) return null;
  const ts = [...props.data.timeSeries].reverse();
  const datasets = [];

  if (selectedMetrics.value.push) {
    datasets.push({
      label: 'Push/s',
      data: ts.map(t => t.pushPerSecond || 0),
      borderColor: 'rgba(255, 107, 0, 0.9)',
      backgroundColor: 'rgba(255, 107, 0, 0.1)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
    });
  }

  if (selectedMetrics.value.pop) {
    datasets.push({
      label: 'Pop/s',
      data: ts.map(t => t.popPerSecond || 0),
      borderColor: 'rgba(99, 102, 241, 0.9)',
      backgroundColor: 'rgba(99, 102, 241, 0.1)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
    });
  }

  if (selectedMetrics.value.ack) {
    datasets.push({
      label: 'Ack/s',
      data: ts.map(t => t.ackPerSecond || 0),
      borderColor: 'rgba(34, 197, 94, 0.9)',
      backgroundColor: 'rgba(34, 197, 94, 0.1)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
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
      grid: { display: false },
      border: { display: false },
      ticks: { maxRotation: 0, autoSkipPadding: 30, font: { size: 10 }, color: '#6b7280' },
    },
    y: {
      beginAtZero: true,
      border: { display: false },
      grid: { color: 'rgba(156, 163, 175, 0.1)' },
      ticks: { font: { size: 10 }, color: '#6b7280', callback: v => v >= 1000 ? `${(v/1000).toFixed(1)}k` : v },
    },
  },
  plugins: {
    legend: { display: false },
    tooltip: {
      backgroundColor: 'rgba(17, 24, 39, 0.95)',
      padding: 10,
      cornerRadius: 6,
      callbacks: {
        label: ctx => ` ${ctx.dataset.label}: ${ctx.parsed.y.toFixed(2)}`,
      },
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
.metric-toggle-orange {
  background: rgba(255, 107, 0, 0.12);
  color: #ea580c;
  border: 1px solid rgba(255, 107, 0, 0.3);
}
.metric-toggle-indigo {
  background: rgba(99, 102, 241, 0.12);
  color: #6366f1;
  border: 1px solid rgba(99, 102, 241, 0.3);
}
.metric-toggle-green {
  background: rgba(34, 197, 94, 0.12);
  color: #16a34a;
  border: 1px solid rgba(34, 197, 94, 0.3);
}
.metric-dot {
  width: 0.375rem;
  height: 0.375rem;
  border-radius: 50%;
}
</style>

