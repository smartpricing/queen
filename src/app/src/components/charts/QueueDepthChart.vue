<template>
  <div class="chart-container">
    <Bar v-if="chartData" :data="chartData" :options="chartOptions" />
    <div v-else class="chart-empty">
      <i class="pi pi-chart-bar text-4xl text-gray-500"></i>
      <p>No queue data available</p>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { Bar } from 'vue-chartjs'
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
    type: Array,
    default: () => []
  }
})

const chartData = computed(() => {
  if (!props.data || props.data.length === 0) return null
  
  // Take top 10 queues by depth
  const sortedData = [...props.data]
    .sort((a, b) => b.depth - a.depth)
    .slice(0, 10)
  
  const labels = sortedData.map(d => {
    const parts = d.queue.split('/')
    return parts.length > 2 ? `.../${parts.slice(-2).join('/')}` : d.queue
  })
  
  return {
    labels,
    datasets: [
      {
        label: 'Pending',
        data: sortedData.map(d => d.depth),
        backgroundColor: '#6366f1',
        borderRadius: 4
      },
      {
        label: 'Processing',
        data: sortedData.map(d => d.processing || 0),
        backgroundColor: '#f59e0b',
        borderRadius: 4
      }
    ]
  }
})

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  indexAxis: 'y',
  plugins: {
    legend: {
      position: 'top',
      labels: {
        color: '#a3a3a3',
        usePointStyle: true,
        padding: 15
      }
    },
    tooltip: {
      backgroundColor: 'rgba(0, 0, 0, 0.8)',
      titleColor: '#fff',
      bodyColor: '#fff',
      borderColor: '#6366f1',
      borderWidth: 1,
      padding: 12
    }
  },
  scales: {
    x: {
      stacked: true,
      grid: {
        color: 'rgba(255, 255, 255, 0.05)',
        drawBorder: false
      },
      ticks: {
        color: '#737373'
      }
    },
    y: {
      stacked: true,
      grid: {
        display: false,
        drawBorder: false
      },
      ticks: {
        color: '#a3a3a3'
      }
    }
  }
}
</script>

<style scoped>
.chart-container {
  width: 100%;
  height: 100%;
  position: relative;
}

.chart-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: #737373;
}
</style>
