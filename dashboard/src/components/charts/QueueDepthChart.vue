<template>
  <div class="queue-depth-chart">
    <Chart 
      v-if="!loading && chartData && chartData.labels && chartData.labels.length > 0"
      type="bar" 
      :data="chartData" 
      :options="chartOptions"
      class="chart"
    />
    <div v-else-if="loading" class="chart-loading">
      <ProgressSpinner />
      <span>Loading chart data...</span>
    </div>
    <div v-else class="chart-empty">
      <i class="pi pi-chart-bar empty-icon"></i>
      <span>No queues with messages</span>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import Chart from 'primevue/chart'
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
  data: {
    type: Object,
    default: null
  },
  loading: {
    type: Boolean,
    default: false
  }
})

const chartData = computed(() => props.data)

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      position: 'bottom',
      labels: {
        padding: 15,
        usePointStyle: true,
        font: {
          size: 12
        }
      }
    },
    tooltip: {
      callbacks: {
        label: (context) => {
          return `${context.dataset.label}: ${context.parsed.y} messages`
        }
      }
    }
  },
  scales: {
    x: {
      stacked: true,
      grid: {
        display: false
      },
      ticks: {
        maxRotation: 45,
        minRotation: 45,
        font: {
          size: 11
        },
        callback: function(value, index) {
          const label = this.getLabelForValue(value)
          return label.length > 15 ? label.substr(0, 15) + '...' : label
        }
      }
    },
    y: {
      stacked: true,
      beginAtZero: true,
      grid: {
        color: 'rgba(0, 0, 0, 0.05)'
      },
      ticks: {
        callback: (value) => {
          if (value >= 1000) {
            return (value / 1000).toFixed(1) + 'K'
          }
          return value
        },
        font: {
          size: 11
        }
      }
    }
  }
}
</script>

<style scoped>
.queue-depth-chart {
  height: 100%;
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
  color: var(--gray-500);
}

.empty-icon {
  font-size: 3rem;
  color: var(--gray-300);
}
</style>
