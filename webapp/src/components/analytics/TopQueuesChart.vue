<template>
  <div class="h-80">
    <Bar v-if="chartData" :data="chartData" :options="chartOptions" />
    <div v-else class="flex items-center justify-center h-full text-gray-500 text-sm">
      No queue data available
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
  queues: Array,
});

const chartData = computed(() => {
  if (!props.queues?.length) return null;

  // Take top 10 queues by total messages
  const topQueues = [...props.queues]
    .sort((a, b) => (b.messages?.total || 0) - (a.messages?.total || 0))
    .slice(0, 10);
  
  return {
    labels: topQueues.map(q => q.name),
    datasets: [
      {
        label: 'Total Messages',
        data: topQueues.map(q => q.messages?.total || 0),
        backgroundColor: createGradientBars(),
        borderColor: 'transparent',
        borderWidth: 0,
        borderRadius: 8,
        barThickness: 24,
      },
    ],
  };
});

// Create gradient for bars
function createGradientBars() {
  return (context) => {
    const chart = context.chart;
    const {ctx, chartArea} = chart;
    
    if (!chartArea) {
      return colors.secondary.rgba(0.8);
    }
    
    const gradient = ctx.createLinearGradient(chartArea.left, 0, chartArea.right, 0);
    gradient.addColorStop(0, 'rgba(255, 107, 0, 0.8)');
    gradient.addColorStop(0.5, 'rgba(79, 70, 229, 0.8)');
    gradient.addColorStop(1, 'rgba(99, 102, 241, 0.8)');
    
    return gradient;
  };
}

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  indexAxis: 'y',
  scales: {
    x: {
      beginAtZero: true,
      grid: {
        display: false,
      },
      border: {
        display: false,
      },
      ticks: {
        font: {
          size: 11,
        },
        color: '#6b7280',
      },
    },
    y: {
      grid: {
        display: false,
      },
      border: {
        display: false,
      },
      ticks: {
        font: {
          size: 11,
        },
        color: '#6b7280',
      },
    },
  },
  plugins: {
    legend: {
      display: false,
    },
    tooltip: {
      backgroundColor: 'rgba(0, 0, 0, 0.9)',
      padding: 12,
      cornerRadius: 8,
      titleFont: {
        size: 13,
      },
      bodyFont: {
        size: 12,
      },
    },
  },
};
</script>
