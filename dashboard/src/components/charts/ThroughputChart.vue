<template>
  <div class="throughput-chart">
    <Chart 
      v-if="!loading && chartData && chartData.labels && chartData.labels.length > 0"
      type="line" 
      :data="chartData" 
      :options="chartOptions"
      class="chart"
    />
    <div v-else-if="loading" class="chart-loading">
      <ProgressSpinner />
      <span>Loading chart data...</span>
    </div>
    <div v-else class="chart-empty">
      <i class="pi pi-chart-line empty-icon"></i>
      <span>No data available</span>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted } from 'vue'
import Chart from 'primevue/chart'
import ProgressSpinner from 'primevue/progressspinner'

// Import and register Chart.js components
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler
} from 'chart.js'

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler
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
        padding: window.innerWidth < 768 ? 10 : 15,
        usePointStyle: true,
        font: {
          size: window.innerWidth < 768 ? 10 : 12
        }
      }
    },
    tooltip: {
      mode: 'index',
      intersect: false,
      callbacks: {
        label: (context) => {
          return `${context.dataset.label}: ${context.parsed.y} msg/min`
        }
      }
    }
  },
  scales: {
    x: {
      grid: {
        display: false
      },
      ticks: {
        maxRotation: 0,
        autoSkipPadding: 20,
        font: {
          size: 11
        }
      }
    },
    y: {
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
  },
  interaction: {
    mode: 'nearest',
    axis: 'x',
    intersect: false
  }
}
</script>

<style scoped>
.throughput-chart {
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
