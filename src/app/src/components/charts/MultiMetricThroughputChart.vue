<template>
  <div class="chart-container">
    <div v-if="!chartData" class="chart-empty">
      <i class="pi pi-chart-line text-4xl text-gray-500"></i>
      <p>No throughput data available</p>
    </div>
    <div v-else class="multi-chart-wrapper">
      <!-- Chart Controls -->
      <div class="chart-controls">
        <div class="metric-toggles">
          <label class="metric-toggle" v-for="metric in metrics" :key="metric.key">
            <Checkbox 
              v-model="visibleMetrics" 
              :value="metric.key" 
              :inputId="metric.key"
            />
            <label :for="metric.key" class="ml-2">
              <span class="metric-badge" :style="{ backgroundColor: metric.color }"></span>
              {{ metric.label }}
            </label>
          </label>
        </div>
        <div class="chart-options">
          <SelectButton 
            v-model="displayMode" 
            :options="displayModes"
            optionLabel="label"
            optionValue="value"
            size="small"
          />
        </div>
      </div>
      
      <!-- Main Throughput Chart -->
      <div class="main-chart">
        <Line :data="chartData" :options="chartOptions" />
      </div>
      
      <!-- Lag Chart (separate scale) -->
      <div v-if="visibleMetrics.includes('lag')" class="lag-chart">
        <h4 class="chart-subtitle">Average Processing Lag</h4>
        <Line :data="lagChartData" :options="lagChartOptions" />
      </div>
      
      <!-- Summary Stats -->
      <div class="summary-stats">
        <div class="stat-card" v-for="stat in summaryStats" :key="stat.label">
          <div class="stat-label">{{ stat.label }}</div>
          <div class="stat-value" :style="{ color: stat.color }">
            {{ stat.value }}
          </div>
          <div class="stat-unit">{{ stat.unit }}</div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { Line } from 'vue-chartjs'
import Checkbox from 'primevue/checkbox'
import SelectButton from 'primevue/selectbutton'
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
import { format } from 'date-fns'

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
    type: Array,
    default: () => []
  },
  showLag: {
    type: Boolean,
    default: true
  }
})

const displayMode = ref('perSecond')
const displayModes = [
  { label: 'Per Second', value: 'perSecond' },
  { label: 'Per Minute', value: 'perMinute' }
]

const metrics = [
  { key: 'incoming', label: 'Incoming', color: '#3b82f6' },
  { key: 'completed', label: 'Completed', color: '#10b981' },
  { key: 'processing', label: 'Processing', color: '#f59e0b' },
  { key: 'failed', label: 'Failed', color: '#ef4444' },
  { key: 'lag', label: 'Lag', color: '#a855f7' }
]

const visibleMetrics = ref(['incoming', 'completed', 'processing', 'failed'])

// Add lag to visible metrics if showLag is true
watch(() => props.showLag, (newVal) => {
  if (newVal && !visibleMetrics.value.includes('lag')) {
    visibleMetrics.value.push('lag')
  } else if (!newVal) {
    visibleMetrics.value = visibleMetrics.value.filter(m => m !== 'lag')
  }
}, { immediate: true })

const chartData = computed(() => {
  if (!props.data || props.data.length === 0) return null
  
  // Sort data chronologically for display
  const sortedData = [...props.data].sort((a, b) => 
    new Date(a.timestamp) - new Date(b.timestamp)
  )
  
  const labels = sortedData.map(d => 
    format(new Date(d.timestamp), 'HH:mm')
  )
  
  const datasets = []
  const metricField = displayMode.value === 'perSecond' ? 'messagesPerSecond' : 'messagesPerMinute'
  
  // Add datasets for visible metrics (except lag which has its own chart)
  if (visibleMetrics.value.includes('incoming')) {
    datasets.push({
      label: 'Incoming',
      data: sortedData.map(d => d.incoming?.[metricField] || 0),
      borderColor: '#3b82f6',
      backgroundColor: 'rgba(59, 130, 246, 0.1)',
      borderWidth: 2,
      fill: false,
      tension: 0.4,
      pointRadius: 0,
      pointHoverRadius: 4
    })
  }
  
  if (visibleMetrics.value.includes('completed')) {
    datasets.push({
      label: 'Completed',
      data: sortedData.map(d => d.completed?.[metricField] || 0),
      borderColor: '#10b981',
      backgroundColor: 'rgba(16, 185, 129, 0.1)',
      borderWidth: 2,
      fill: false,
      tension: 0.4,
      pointRadius: 0,
      pointHoverRadius: 4
    })
  }
  
  if (visibleMetrics.value.includes('processing')) {
    datasets.push({
      label: 'Processing',
      data: sortedData.map(d => d.processing?.[metricField] || 0),
      borderColor: '#f59e0b',
      backgroundColor: 'rgba(245, 158, 11, 0.1)',
      borderWidth: 2,
      fill: false,
      tension: 0.4,
      pointRadius: 0,
      pointHoverRadius: 4
    })
  }
  
  if (visibleMetrics.value.includes('failed')) {
    datasets.push({
      label: 'Failed',
      data: sortedData.map(d => d.failed?.[metricField] || 0),
      borderColor: '#ef4444',
      backgroundColor: 'rgba(239, 68, 68, 0.1)',
      borderWidth: 2,
      fill: false,
      tension: 0.4,
      pointRadius: 0,
      pointHoverRadius: 4
    })
  }
  
  return {
    labels,
    datasets
  }
})

const lagChartData = computed(() => {
  if (!props.data || props.data.length === 0) return null
  
  const sortedData = [...props.data].sort((a, b) => 
    new Date(a.timestamp) - new Date(b.timestamp)
  )
  
  const labels = sortedData.map(d => 
    format(new Date(d.timestamp), 'HH:mm')
  )
  
  return {
    labels,
    datasets: [{
      label: 'Avg Lag (seconds)',
      data: sortedData.map(d => d.lag?.avgSeconds || 0),
      borderColor: '#a855f7',
      backgroundColor: 'rgba(168, 85, 247, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0.4,
      pointRadius: 0,
      pointHoverRadius: 4
    }]
  }
})

const summaryStats = computed(() => {
  if (!props.data || props.data.length === 0) return []
  
  // Get the most recent data point for current stats
  const latest = props.data[0] // Data is sorted newest first from API
  const stats = []
  
  if (visibleMetrics.value.includes('incoming')) {
    const total = props.data.reduce((sum, d) => sum + (d.incoming?.messagesPerMinute || 0), 0)
    stats.push({
      label: 'Total Incoming',
      value: total.toLocaleString(),
      unit: 'messages',
      color: '#3b82f6'
    })
  }
  
  if (visibleMetrics.value.includes('completed')) {
    const total = props.data.reduce((sum, d) => sum + (d.completed?.messagesPerMinute || 0), 0)
    stats.push({
      label: 'Total Completed',
      value: total.toLocaleString(),
      unit: 'messages',
      color: '#10b981'
    })
  }
  
  if (visibleMetrics.value.includes('failed')) {
    const total = props.data.reduce((sum, d) => sum + (d.failed?.messagesPerMinute || 0), 0)
    stats.push({
      label: 'Total Failed',
      value: total.toLocaleString(),
      unit: 'messages',
      color: '#ef4444'
    })
  }
  
  if (visibleMetrics.value.includes('lag')) {
    const avgLag = props.data.reduce((sum, d, idx, arr) => {
      if (d.lag?.sampleCount > 0) {
        return sum + (d.lag.avgSeconds || 0)
      }
      return sum
    }, 0)
    const validSamples = props.data.filter(d => d.lag?.sampleCount > 0).length
    const overallAvgLag = validSamples > 0 ? avgLag / validSamples : 0
    
    stats.push({
      label: 'Avg Lag',
      value: overallAvgLag > 1 ? overallAvgLag.toFixed(1) : (overallAvgLag * 1000).toFixed(0),
      unit: overallAvgLag > 1 ? 'seconds' : 'ms',
      color: '#a855f7'
    })
  }
  
  return stats
})

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: {
    mode: 'index',
    intersect: false
  },
  plugins: {
    legend: {
      display: true,
      position: 'top',
      labels: {
        color: '#d1d5db',
        usePointStyle: true,
        padding: 15
      }
    },
    tooltip: {
      backgroundColor: 'rgba(0, 0, 0, 0.8)',
      titleColor: '#fff',
      bodyColor: '#fff',
      borderColor: '#374151',
      borderWidth: 1,
      padding: 12,
      displayColors: true,
      callbacks: {
        label: (context) => {
          const unit = displayMode.value === 'perSecond' ? 'msg/s' : 'msg/min'
          return `${context.dataset.label}: ${context.parsed.y} ${unit}`
        }
      }
    }
  },
  scales: {
    x: {
      grid: {
        color: 'rgba(255, 255, 255, 0.05)',
        drawBorder: false
      },
      ticks: {
        color: '#737373',
        maxRotation: 45,
        minRotation: 0
      }
    },
    y: {
      beginAtZero: true,
      grid: {
        color: 'rgba(255, 255, 255, 0.05)',
        drawBorder: false
      },
      ticks: {
        color: '#737373',
        callback: (value) => {
          const unit = displayMode.value === 'perSecond' ? 'msg/s' : ''
          return `${value}${unit}`
        }
      },
      title: {
        display: true,
        text: displayMode.value === 'perSecond' ? 'Messages per Second' : 'Messages per Minute',
        color: '#737373'
      }
    }
  }
}

const lagChartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: {
    mode: 'index',
    intersect: false
  },
  plugins: {
    legend: {
      display: false
    },
    tooltip: {
      backgroundColor: 'rgba(0, 0, 0, 0.8)',
      titleColor: '#fff',
      bodyColor: '#fff',
      borderColor: '#a855f7',
      borderWidth: 1,
      padding: 12,
      displayColors: false,
      callbacks: {
        label: (context) => {
          const value = context.parsed.y
          if (value > 1) {
            return `Lag: ${value.toFixed(2)} seconds`
          } else {
            return `Lag: ${(value * 1000).toFixed(0)} ms`
          }
        }
      }
    }
  },
  scales: {
    x: {
      grid: {
        color: 'rgba(255, 255, 255, 0.05)',
        drawBorder: false
      },
      ticks: {
        color: '#737373',
        maxRotation: 45,
        minRotation: 0
      }
    },
    y: {
      beginAtZero: true,
      grid: {
        color: 'rgba(255, 255, 255, 0.05)',
        drawBorder: false
      },
      ticks: {
        color: '#737373',
        callback: (value) => {
          if (value > 1) {
            return `${value}s`
          } else {
            return `${(value * 1000).toFixed(0)}ms`
          }
        }
      },
      title: {
        display: true,
        text: 'Processing Lag',
        color: '#737373'
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

.multi-chart-wrapper {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  height: 100%;
}

.chart-controls {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem;
  background: var(--surface-100);
  border-radius: 6px;
  flex-wrap: wrap;
  gap: 1rem;
}

.metric-toggles {
  display: flex;
  gap: 1.5rem;
  flex-wrap: wrap;
}

.metric-toggle {
  display: flex;
  align-items: center;
  cursor: pointer;
}

.metric-toggle label {
  display: flex;
  align-items: center;
  cursor: pointer;
  font-size: 0.875rem;
}

.metric-badge {
  width: 12px;
  height: 12px;
  border-radius: 2px;
  margin-right: 0.5rem;
  display: inline-block;
}

.chart-options {
  display: flex;
  gap: 0.5rem;
}

.main-chart {
  flex: 1;
  min-height: 250px;
  position: relative;
}

.lag-chart {
  height: 150px;
  position: relative;
  padding-top: 1rem;
  border-top: 1px solid var(--surface-200);
}

.chart-subtitle {
  margin: 0 0 0.5rem 0;
  font-size: 0.875rem;
  color: #a855f7;
  font-weight: 500;
}

.summary-stats {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
  gap: 1rem;
  padding: 1rem 0;
  border-top: 1px solid var(--surface-200);
}

.stat-card {
  text-align: center;
  padding: 0.75rem;
  background: var(--surface-100);
  border-radius: 6px;
}

.stat-label {
  font-size: 0.75rem;
  color: #737373;
  margin-bottom: 0.25rem;
}

.stat-value {
  font-size: 1.25rem;
  font-weight: 600;
  margin-bottom: 0.25rem;
}

.stat-unit {
  font-size: 0.75rem;
  color: #737373;
}

.chart-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: #737373;
}

@media (max-width: 768px) {
  .chart-controls {
    flex-direction: column;
    align-items: flex-start;
  }
  
  .metric-toggles {
    flex-direction: column;
    gap: 0.75rem;
  }
  
  .summary-stats {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
