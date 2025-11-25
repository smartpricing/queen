<template>
  <div class="h-80 flex items-center justify-center p-8">
    <div class="w-full max-w-sm">
      <Doughnut v-if="chartData" :data="chartData" :options="chartOptions" />
      <div v-else class="text-center text-gray-500 text-sm">
        No message data available
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { Doughnut } from 'vue-chartjs';
import {
  Chart as ChartJS,
  ArcElement,
  Tooltip,
  Legend,
} from 'chart.js';
import { getStatusColors } from '../../utils/colors';

ChartJS.register(
  ArcElement,
  Tooltip,
  Legend
);

const props = defineProps({
  messages: Object,
});

const chartData = computed(() => {
  if (!props.messages) return null;

  const data = [
    props.messages.pending || 0,
    props.messages.processing || 0,
    props.messages.completed || 0,
    props.messages.failed || 0,
    props.messages.deadLetter || 0,
  ];
  
  // Only show chart if there's data
  if (data.every(v => v === 0)) return null;
  
  const statusColors = getStatusColors();
  
  return {
    labels: ['Pending', 'Processing', 'Completed', 'Failed', 'Dead Letter'],
    datasets: [
      {
        data: data,
        backgroundColor: statusColors.backgrounds,
        borderColor: statusColors.borders,
        borderWidth: 2,
      },
    ],
  };
});

const chartOptions = {
  responsive: true,
  maintainAspectRatio: true,
  layout: {
    padding: {
      top: 20,
      bottom: 20,
      left: 20,
      right: 20,
    },
  },
  plugins: {
    legend: {
      position: 'bottom',
      labels: {
        padding: 15,
        font: {
          size: 12,
        },
        color: '#6b7280',
        usePointStyle: true,
        pointStyle: 'circle',
      },
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
      callbacks: {
        label: (context) => {
          const label = context.label || '';
          const value = context.parsed || 0;
          const total = context.dataset.data.reduce((a, b) => a + b, 0);
          const percentage = total > 0 ? ((value / total) * 100).toFixed(1) : 0;
          return ` ${label}: ${value.toLocaleString()} (${percentage}%)`;
        },
      },
    },
  },
};
</script>

