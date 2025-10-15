<template>
  <div class="chart-container">
    <Line v-if="chartData" :data="chartData" :options="chartOptions" />
    <div v-else class="flex items-center justify-center h-full text-gray-500 text-sm">
      No data available
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

  // Reverse the array since API returns data in descending order
  const throughput = [...props.data.throughput].reverse();
  
  return {
    labels: throughput.map(t => {
      const date = new Date(t.timestamp);
      return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    }),
    datasets: [
      {
        label: 'Ingested',
        data: throughput.map(t => t.ingestedPerSecond),
        borderColor: 'rgb(59, 130, 246)',
        backgroundColor: 'rgba(59, 130, 246, 0.1)',
        fill: true,
        tension: 0, // No splines - straight lines
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
        pointHoverBackgroundColor: 'rgb(59, 130, 246)',
        pointHoverBorderColor: '#fff',
        pointHoverBorderWidth: 2,
      },
      {
        label: 'Processed',
        data: throughput.map(t => t.processedPerSecond),
        borderColor: 'rgb(16, 185, 129)',
        backgroundColor: 'rgba(16, 185, 129, 0.1)',
        fill: true,
        tension: 0, // No splines - straight lines
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
        pointHoverBackgroundColor: 'rgb(16, 185, 129)',
        pointHoverBorderColor: '#fff',
        pointHoverBorderWidth: 2,
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
        callback: (value) => value.toFixed(1),
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
      callbacks: {
        title: (items) => items[0].label,
        label: (context) => {
          return ` ${context.dataset.label}: ${context.parsed.y.toFixed(2)} msg/s`;
        },
      },
    },
    filler: {
      propagate: true,
    },
  },
  elements: {
    line: {
      tension: 0, // Ensure no curves
    },
  },
};
</script>
