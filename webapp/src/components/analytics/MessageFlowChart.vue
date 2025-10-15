<template>
  <div class="chart-container">
    <Line v-if="chartData" :data="chartData" :options="chartOptions" />
    <div v-else class="flex items-center justify-center h-full text-gray-500 text-sm">
      No data available for this time range
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

const chartData = computed(() => {
  if (!props.data?.throughput?.length) return null;

  const throughput = [...props.data.throughput].reverse();
  
  return {
    labels: throughput.map(t => {
      const date = new Date(t.timestamp);
      return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    }),
    datasets: [
      {
        label: 'Ingested',
        data: throughput.map(t => t.ingested || t.ingestedPerSecond || 0),
        borderColor: 'rgb(59, 130, 246)',
        backgroundColor: 'rgba(59, 130, 246, 0.2)',
        fill: true,
        tension: 0,
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
      },
      {
        label: 'Processed',
        data: throughput.map(t => t.processed || t.processedPerSecond || 0),
        borderColor: 'rgb(16, 185, 129)',
        backgroundColor: 'rgba(16, 185, 129, 0.2)',
        fill: true,
        tension: 0,
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
      },
    ],
  };
});

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: {
    mode: 'index',
    intersect: false,
  },
  scales: {
    x: {
      stacked: false,
      grid: {
        display: false,
      },
      border: {
        display: false,
      },
      ticks: {
        maxRotation: 0,
        autoSkipPadding: 20,
        font: {
          size: 10,
        },
        color: '#9ca3af',
      },
    },
    y: {
      stacked: false,
      beginAtZero: true,
      border: {
        display: false,
      },
      grid: {
        display: false,
      },
      ticks: {
        font: {
          size: 11,
        },
        color: '#9ca3af',
        padding: 8,
      },
    },
  },
  plugins: {
    legend: {
      display: true,
      position: 'top',
      align: 'end',
      labels: {
        boxWidth: 12,
        boxHeight: 12,
        padding: 15,
        usePointStyle: true,
        pointStyle: 'circle',
        font: {
          size: 12,
          weight: '500',
        },
        color: '#6b7280',
      },
    },
    tooltip: {
      enabled: true,
      backgroundColor: 'rgba(0, 0, 0, 0.9)',
      padding: 12,
      cornerRadius: 8,
      titleFont: {
        size: 13,
        weight: '600',
      },
      bodyFont: {
        size: 12,
      },
      bodySpacing: 6,
      usePointStyle: true,
    },
  },
};
</script>

