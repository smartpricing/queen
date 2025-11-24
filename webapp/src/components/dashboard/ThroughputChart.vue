<template>
  <div class="throughput-chart">
    <!-- Metric Selector -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
      <button
        @click="selectedMetrics.ingested = !selectedMetrics.ingested"
        :class="[
          'metric-toggle',
          selectedMetrics.ingested ? 'metric-toggle-active-blue' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.ingested ? 'bg-orange-500' : 'bg-gray-400']"></div>
        Ingested
      </button>
      <button
        @click="selectedMetrics.processed = !selectedMetrics.processed"
        :class="[
          'metric-toggle',
          selectedMetrics.processed ? 'metric-toggle-active-indigo' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.processed ? 'bg-indigo-500' : 'bg-gray-400']"></div>
        Processed
      </button>
    </div>

    <!-- Chart -->
    <div class="chart-container h-52">
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

const selectedMetrics = ref({
  ingested: true,
  processed: true,
});

const chartData = computed(() => {
  if (!props.data?.throughput?.length) return null;

  // Reverse the array since API returns data in descending order
  const throughput = [...props.data.throughput].reverse();
  
  const datasets = [];

  if (selectedMetrics.value.ingested) {
    datasets.push({
      label: 'Ingested',
      data: throughput.map(t => t.ingestedPerSecond),
      backgroundColor: createGradientBars('blue'),
      borderColor: 'transparent',
      borderWidth: 0,
      borderRadius: 3,
      barPercentage: 0.85,
      categoryPercentage: 0.9,
    });
  }

  if (selectedMetrics.value.processed) {
    datasets.push({
      label: 'Processed',
      data: throughput.map(t => t.processedPerSecond),
      backgroundColor: createGradientBars('indigo'),
      borderColor: 'transparent',
      borderWidth: 0,
      borderRadius: 3,
      barPercentage: 0.85,
      categoryPercentage: 0.9,
    });
  }
  
  // Determine if we need to show dates (time range > 24 hours)
  const showDates = isMultiDay();
  
  return {
    labels: throughput.map(t => formatTimestamp(t.timestamp, showDates)),
    datasets,
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

// Create gradient for bars - professional blue theme
function createGradientBars(color) {
  return (context) => {
    const chart = context.chart;
    const {ctx, chartArea} = chart;
    
    if (!chartArea) {
      return color === 'blue' ? colors.charts.ingested.border : colors.charts.processed.border;
    }
    
    const gradient = ctx.createLinearGradient(0, chartArea.top, 0, chartArea.bottom);
    
    if (color === 'blue') {
      // Blue gradient for ingested
      gradient.addColorStop(0, 'rgba(255, 107, 0, 0.8)');
      gradient.addColorStop(1, 'rgba(255, 107, 0, 0.4)');
    } else if (color === 'indigo') {
      // Indigo gradient for processed
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
          size: 11,
          family: 'Inter, -apple-system, sans-serif',
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
        callback: (value) => value.toFixed(1),
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
          return ` ${context.dataset.label}: ${context.parsed.y.toFixed(2)} msg/s`;
        },
      },
    },
  },
};
</script>

<style scoped>
.throughput-chart {
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
  transition: background-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), color 0.15s cubic-bezier(0.4, 0, 0.2, 1);
  cursor: pointer;
  letter-spacing: 0.01em;
}

.metric-toggle-active-blue {
  background: rgba(255, 107, 0, 0.12);
  color: #2563eb;
  border: 1px solid rgba(255, 107, 0, 0.3);
  box-shadow: 0 1px 2px 0 rgba(255, 107, 0, 0.1);
}

.dark .metric-toggle-active-blue {
  background: rgba(255, 107, 0, 0.18);
  color: #FF4081;
  border: 1px solid rgba(255, 107, 0, 0.4);
}

.metric-toggle-active-indigo {
  background: rgba(99, 102, 241, 0.12);
  color: #4f46e5;
  border: 1px solid rgba(99, 102, 241, 0.3);
  box-shadow: 0 1px 2px 0 rgba(99, 102, 241, 0.1);
}

.dark .metric-toggle-active-indigo {
  background: rgba(99, 102, 241, 0.18);
  color: #818cf8;
  border: 1px solid rgba(99, 102, 241, 0.4);
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


.metric-toggle-active-blue:hover {
  background: rgba(255, 107, 0, 0.15);
}

.dark .metric-toggle-active-blue:hover {
  background: rgba(255, 107, 0, 0.2);
}

.metric-toggle-active-indigo:hover {
  background: rgba(99, 102, 241, 0.15);
}

.dark .metric-toggle-active-indigo:hover {
  background: rgba(99, 102, 241, 0.2);
}

.metric-toggle-inactive:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .metric-toggle-inactive:hover {
  background: rgba(255, 255, 255, 0.05);
}

.metric-dot {
  width: 0.4rem;
  height: 0.4rem;
  border-radius: 50%;
}
</style>
