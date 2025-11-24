<template>
  <div class="chart-container">
    <Bar v-if="chartData" :data="chartData" :options="chartOptions" />
    <div v-else class="flex items-center justify-center h-full text-gray-500 text-sm">
      No data available for this time range
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

  const throughput = [...props.data.throughput].reverse();
  
  // Determine if we need to show dates (time range > 24 hours)
  const showDates = isMultiDay();
  
  return {
    labels: throughput.map(t => formatTimestamp(t.timestamp, showDates)),
    datasets: [
      {
        label: 'Ingested',
        data: throughput.map(t => t.ingested || t.ingestedPerSecond || 0),
        backgroundColor: createGradientBars('blue'),
        borderColor: 'transparent',
        borderWidth: 0,
        borderRadius: 3,
        barPercentage: 0.85,
        categoryPercentage: 0.9,
      },
      {
        label: 'Processed',
        data: throughput.map(t => t.processed || t.processedPerSecond || 0),
        backgroundColor: createGradientBars('indigo'),
        borderColor: 'transparent',
        borderWidth: 0,
        borderRadius: 3,
        barPercentage: 0.85,
        categoryPercentage: 0.9,
      },
    ],
  };
});

function isMultiDay() {
  if (!props.data?.timeRange?.from || !props.data?.timeRange?.to) return false;
  const from = new Date(props.data.timeRange.from);
  const to = new Date(props.data.timeRange.to);
  const diffHours = (to - from) / (1000 * 60 * 60);
  return diffHours > 24;
}

function formatTimestamp(ts, showDate) {
  const date = new Date(ts);
  if (showDate) {
    return date.toLocaleString('en-US', { 
      month: 'short', 
      day: 'numeric',
      hour: '2-digit', 
      minute: '2-digit' 
    });
  } else {
    return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
  }
}

function createGradientBars(color) {
  return (context) => {
    const chart = context.chart;
    const {ctx, chartArea} = chart;
    
    if (!chartArea) {
      return color === 'blue' ? colors.charts.ingested.border : colors.charts.processed.border;
    }
    
    const gradient = ctx.createLinearGradient(0, chartArea.top, 0, chartArea.bottom);
    
    if (color === 'blue') {
      gradient.addColorStop(0, 'rgba(255, 107, 0, 0.8)');
      gradient.addColorStop(1, 'rgba(255, 107, 0, 0.4)');
    } else if (color === 'indigo') {
      gradient.addColorStop(0, 'rgba(99, 102, 241, 0.8)');
      gradient.addColorStop(1, 'rgba(99, 102, 241, 0.4)');
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
        maxRotation: 45,
        minRotation: 0,
        autoSkipPadding: 30,
        font: {
          size: 10,
        },
        color: '#6b7280',
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
        color: '#6b7280',
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
      callbacks: {
        title: function(context) {
          if (context[0]?.label) {
            return context[0].label;
          }
          return '';
        },
      },
    },
  },
};
</script>
