<template>
  <div class="chart-container">
    <div class="chart-header">
      <h3 class="chart-title">{{ title || 'Message Throughput' }}</h3>
      <div class="chart-actions">
        <Button 
          icon="pi pi-refresh" 
          class="p-button-text p-button-sm"
          @click="$emit('refresh')"
          v-tooltip="'Refresh'"
        />
      </div>
    </div>
    
    <div class="throughput-chart">
      <Chart 
        v-if="!loading && chartDataWithTheme && chartDataWithTheme.labels && chartDataWithTheme.labels.length > 0"
        type="bar" 
        :data="chartDataWithTheme" 
        :options="chartOptionsWithTheme"
        class="chart"
      />
      <div v-else-if="loading" class="chart-loading">
        <ProgressSpinner />
        <span>Loading chart data...</span>
      </div>
      <div v-else class="chart-empty">
        <i class="pi pi-chart-bar empty-icon"></i>
        <span>No data available</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import Chart from 'primevue/chart'
import Button from 'primevue/button'
import ProgressSpinner from 'primevue/progressspinner'

// Import and register Chart.js components
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  BarElement,
  Title,
  Tooltip,
  Legend
} from 'chart.js'

ChartJS.register(
  CategoryScale,
  LinearScale,
  BarElement,
  Title,
  Tooltip,
  Legend
)

const props = defineProps({
  title: {
    type: String,
    default: null
  },
  data: {
    type: Object,
    default: null
  },
  loading: {
    type: Boolean,
    default: false
  },
  stacked: {
    type: Boolean,
    default: true
  }
})

const emit = defineEmits(['refresh'])

// Apply dark theme colors to chart data
const chartDataWithTheme = computed(() => {
  if (!props.data) return null
  
  return {
    labels: props.data.labels,
    datasets: [
      {
        label: 'Incoming Messages',
        backgroundColor: '#ec4899',
        borderColor: '#ec4899',
        borderWidth: 0,
        borderRadius: 4,
        data: props.data.datasets?.[0]?.data || []
      },
      {
        label: 'Completed Messages',
        backgroundColor: 'rgba(236, 72, 153, 0.5)',
        borderColor: 'rgba(236, 72, 153, 0.5)',
        borderWidth: 0,
        borderRadius: 4,
        data: props.data.datasets?.[1]?.data || []
      },
      {
        label: 'Failed Messages',
        backgroundColor: 'rgba(236, 72, 153, 0.2)',
        borderColor: 'rgba(236, 72, 153, 0.2)',
        borderWidth: 0,
        borderRadius: 4,
        data: props.data.datasets?.[2]?.data || []
      }
    ]
  }
})

// Dark theme chart options
const chartOptionsWithTheme = computed(() => ({
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      position: 'bottom',
      labels: {
        padding: 15,
        usePointStyle: true,
        font: {
          size: 11,
          family: 'Inter'
        },
        color: '#cbd5e1'
      }
    },
    tooltip: {
      backgroundColor: 'rgba(30, 41, 59, 0.95)',
      titleColor: '#f1f5f9',
      bodyColor: '#cbd5e1',
      borderColor: 'rgba(236, 72, 153, 0.3)',
      borderWidth: 1,
      cornerRadius: 8,
      padding: 12,
      displayColors: true,
      mode: 'index',
      intersect: false,
      callbacks: {
        label: (context) => {
          return `${context.dataset.label}: ${context.parsed.y.toLocaleString()} messages`
        }
      }
    }
  },
  scales: {
    x: {
      stacked: props.stacked,
      grid: {
        display: false
      },
      ticks: {
        color: '#94a3b8',
        maxRotation: 0,
        autoSkipPadding: 20,
        font: {
          size: 11
        }
      },
      border: {
        display: false
      }
    },
    y: {
      stacked: props.stacked,
      beginAtZero: true,
      grid: {
        color: 'rgba(255, 255, 255, 0.03)',
        drawBorder: false
      },
      ticks: {
        color: '#94a3b8',
        callback: (value) => {
          if (value >= 1000000) {
            return (value / 1000000).toFixed(1) + 'M'
          } else if (value >= 1000) {
            return (value / 1000).toFixed(1) + 'K'
          }
          return value
        },
        font: {
          size: 11
        }
      },
      border: {
        display: false
      }
    }
  },
  interaction: {
    mode: 'index',
    intersect: false
  },
  animation: {
    duration: 300,
    easing: 'easeInOutQuart',
    // Smooth animations for real-time updates
    onComplete: null,
    onProgress: null
  },
  // Enable smooth transitions for data updates
  transitions: {
    active: {
      animation: {
        duration: 200
      }
    }
  }
}))
</script>

<style scoped>
.chart-container {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 1.5rem;
  height: 100%;
  display: flex;
  flex-direction: column;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.chart-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--surface-700);
  margin: 0;
}

.chart-actions {
  display: flex;
  gap: 0.5rem;
}

.throughput-chart {
  flex: 1;
  min-height: 300px;
  position: relative;
}

.chart {
  height: 100%;
}

.chart-loading,
.chart-empty {
  height: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1rem;
  color: var(--surface-400);
}

.empty-icon {
  font-size: 3rem;
  color: var(--surface-300);
}

:deep(.p-button-sm) {
  padding: 0.5rem;
  font-size: 0.875rem;
}

:deep(.p-button-text) {
  color: var(--surface-500);
}

:deep(.p-button-text:hover) {
  background: rgba(236, 72, 153, 0.1);
  color: var(--primary-500);
}

/* Responsive */
@media (max-width: 768px) {
  .chart-container {
    padding: 1rem;
  }
  
  .chart-title {
    font-size: 1rem;
  }
  
  .throughput-chart {
    min-height: 250px;
  }
}
</style>