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
  { key: 'avgSlots', label: 'Avg Free', activeClass: 'metric-toggle-cyan', dotClass: 'bg-cyan-500' },
  { key: 'minSlots', label: 'Min Free', activeClass: 'metric-toggle-rose', dotClass: 'bg-rose-500' },
  { key: 'jobQueue', label: 'Job Queue', activeClass: 'metric-toggle-amber', dotClass: 'bg-amber-500' },
  { key: 'backoff', label: 'Backoff', activeClass: 'metric-toggle-violet', dotClass: 'bg-violet-500' },
];

const selectedMetrics = ref({ avgSlots: true, minSlots: true, jobQueue: false, backoff: false });

function toggleMetric(key) {
  selectedMetrics.value[key] = !selectedMetrics.value[key];
}

const chartData = computed(() => {
  if (!props.data?.timeSeries?.length) return null;
  const ts = [...props.data.timeSeries].reverse();
  const datasets = [];

  if (selectedMetrics.value.avgSlots) {
    datasets.push({
      label: 'Avg Free Slots',
      data: ts.map(t => t.avgFreeSlots || 0),
      borderColor: 'rgba(6, 182, 212, 0.9)',
      backgroundColor: 'rgba(6, 182, 212, 0.1)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
    });
  }

  if (selectedMetrics.value.minSlots) {
    datasets.push({
      label: 'Min Free Slots',
      data: ts.map(t => t.minFreeSlots || 0),
      borderColor: 'rgba(244, 63, 94, 0.9)',
      backgroundColor: 'rgba(244, 63, 94, 0.05)',
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: true,
    });
  }

  if (selectedMetrics.value.jobQueue) {
    datasets.push({
      label: 'Max Job Queue',
      data: ts.map(t => t.maxJobQueueSize || 0),
      borderColor: 'rgba(245, 158, 11, 0.9)',
      backgroundColor: 'rgba(245, 158, 11, 0.05)',
      borderWidth: 2,
      borderDash: [5, 3],
      pointRadius: 0,
      pointHoverRadius: 4,
      tension: 0.3,
      fill: false,
    });
  }

  if (selectedMetrics.value.backoff) {
    datasets.push({
      label: 'Backoff (Idle Consumers)',
      data: ts.map(t => t.backoffSize || 0),
      borderColor: 'rgba(139, 92, 246, 0.9)',
      backgroundColor: 'rgba(139, 92, 246, 0.1)',
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
.metric-toggle-cyan {
  background: rgba(6, 182, 212, 0.12);
  color: #0891b2;
  border: 1px solid rgba(6, 182, 212, 0.3);
}
.metric-toggle-rose {
  background: rgba(244, 63, 94, 0.12);
  color: #e11d48;
  border: 1px solid rgba(244, 63, 94, 0.3);
}
.metric-toggle-amber {
  background: rgba(245, 158, 11, 0.12);
  color: #d97706;
  border: 1px solid rgba(245, 158, 11, 0.3);
}
.metric-toggle-violet {
  background: rgba(139, 92, 246, 0.12);
  color: #7c3aed;
  border: 1px solid rgba(139, 92, 246, 0.3);
}
.metric-dot {
  width: 0.375rem;
  height: 0.375rem;
  border-radius: 50%;
}
</style>

