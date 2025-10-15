<template>
  <div class="chart-container">
    <Bar v-if="chartData" :data="chartData" :options="chartOptions" />
    <div v-else class="flex items-center justify-center h-full text-gray-500 text-sm">
      No data available
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
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
import { colors } from '../../utils/colors';

ChartJS.register(
  CategoryScale,
  LinearScale,
  BarElement,
  Title,
  Tooltip,
  Legend
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
        backgroundColor: createGradientBars('rose'),
        borderColor: 'transparent',
        borderWidth: 0,
        borderRadius: 4,
        barPercentage: 0.9,
        categoryPercentage: 0.95,
      },
      {
        label: 'Processed',
        data: throughput.map(t => t.processedPerSecond),
        backgroundColor: createGradientBars('purple'),
        borderColor: 'transparent',
        borderWidth: 0,
        borderRadius: 4,
        barPercentage: 0.9,
        categoryPercentage: 0.95,
      },
    ],
  };
});

// Create gradient for bars
function createGradientBars(color) {
  return (context) => {
    const chart = context.chart;
    const {ctx, chartArea} = chart;
    
    if (!chartArea) {
      return color === 'rose' ? colors.charts.ingested.border : colors.charts.processed.border;
    }
    
    const gradient = ctx.createLinearGradient(0, chartArea.top, 0, chartArea.bottom);
    
    if (color === 'rose') {
      gradient.addColorStop(0, 'rgba(244, 63, 94, 0.9)');
      gradient.addColorStop(1, 'rgba(244, 63, 94, 0.5)');
    } else if (color === 'purple') {
      gradient.addColorStop(0, 'rgba(168, 85, 247, 0.9)');
      gradient.addColorStop(1, 'rgba(168, 85, 247, 0.5)');
    }
    
    return gradient;
  };
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
  },
};
</script>
