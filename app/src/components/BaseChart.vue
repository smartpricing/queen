<template>
  <div class="w-full" :style="{ height: height }">
    <canvas ref="chartCanvas" />
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted, computed, nextTick } from 'vue'
import { 
  Chart, 
  LineController, 
  BarController, 
  DoughnutController,
  ArcElement,
  LineElement, 
  BarElement, 
  PointElement, 
  LinearScale, 
  CategoryScale,
  TimeScale,
  Tooltip, 
  Legend, 
  Filler 
} from 'chart.js'
import { useTheme } from '@/composables/useTheme'

// Register Chart.js components
Chart.register(
  LineController, 
  BarController, 
  DoughnutController,
  ArcElement,
  LineElement, 
  BarElement, 
  PointElement, 
  LinearScale, 
  CategoryScale,
  TimeScale,
  Tooltip, 
  Legend, 
  Filler
)

const props = defineProps({
  type: { type: String, default: 'line' },
  data: { type: Object, required: true },
  options: { type: Object, default: () => ({}) },
  height: { type: String, default: '280px' },
})

const chartCanvas = ref(null)
const { isDark } = useTheme()

let chart = null

// Color palette
const colors = {
  queen: {
    main: '#EC4899',
    light: 'rgba(236, 72, 153, 0.1)',
    gradient: ['rgba(236, 72, 153, 0.3)', 'rgba(236, 72, 153, 0)']
  },
  cyber: {
    main: '#06B6D4',
    light: 'rgba(6, 182, 212, 0.1)',
    gradient: ['rgba(6, 182, 212, 0.3)', 'rgba(6, 182, 212, 0)']
  },
  crown: {
    main: '#F59E0B',
    light: 'rgba(245, 158, 11, 0.1)',
    gradient: ['rgba(245, 158, 11, 0.3)', 'rgba(245, 158, 11, 0)']
  },
  emerald: {
    main: '#10B981',
    light: 'rgba(16, 185, 129, 0.1)',
    gradient: ['rgba(16, 185, 129, 0.3)', 'rgba(16, 185, 129, 0)']
  },
  rose: {
    main: '#F43F5E',
    light: 'rgba(244, 63, 94, 0.1)',
    gradient: ['rgba(244, 63, 94, 0.3)', 'rgba(244, 63, 94, 0)']
  }
}

const getDefaultOptions = () => ({
  responsive: true,
  maintainAspectRatio: false,
  interaction: {
    mode: 'index',
    intersect: false,
  },
  plugins: {
    legend: {
      display: false,
    },
    tooltip: {
      backgroundColor: isDark.value ? '#1f2937' : '#ffffff',
      titleColor: isDark.value ? '#f3f4f6' : '#111827',
      bodyColor: isDark.value ? '#d1d5db' : '#4b5563',
      borderColor: isDark.value ? '#374151' : '#e5e7eb',
      borderWidth: 1,
      padding: 12,
      cornerRadius: 8,
      displayColors: true,
      usePointStyle: true,
      boxPadding: 6,
    }
  },
  scales: props.type !== 'doughnut' ? {
    x: {
      grid: {
        display: false,
      },
      ticks: {
        color: isDark.value ? '#9ca3af' : '#6b7280',
        font: {
          size: 11,
        },
        maxRotation: 0,
        autoSkip: true,
        maxTicksLimit: 12,
      },
      border: {
        display: false,
      }
    },
    y: {
      display: true,
      min: 0,
      beginAtZero: true,
      grid: {
        color: isDark.value ? 'rgba(255, 255, 255, 0.05)' : 'rgba(0, 0, 0, 0.05)',
        drawBorder: false,
      },
      ticks: {
        display: true,
        color: isDark.value ? '#9ca3af' : '#6b7280',
        font: {
          size: 11,
        },
        padding: 8,
      },
      border: {
        display: false,
      }
    }
  } : undefined,
})

// Check if data is valid for rendering
const hasValidData = computed(() => {
  if (!props.data) return false
  if (!props.data.labels || !props.data.datasets) return false
  if (props.data.labels.length === 0) return false
  return true
})

const processChartData = (ctx) => {
  if (!hasValidData.value) {
    return { labels: [], datasets: [] }
  }
  
  let chartData = JSON.parse(JSON.stringify(props.data)) // Deep clone to avoid mutation
  
  if (props.type === 'line' && chartData.datasets) {
    chartData.datasets = chartData.datasets.map((dataset, index) => {
      const colorKey = ['queen', 'cyber', 'crown', 'emerald', 'rose'][index % 5]
      const color = colors[colorKey]
      
      // Create gradient
      let backgroundColor = color.light
      if (ctx && dataset.fill !== false) {
        const gradient = ctx.createLinearGradient(0, 0, 0, 280)
        gradient.addColorStop(0, color.gradient[0])
        gradient.addColorStop(1, color.gradient[1])
        backgroundColor = gradient
      }
      
      return {
        ...dataset,
        borderColor: dataset.borderColor || color.main,
        backgroundColor: backgroundColor,
        pointBackgroundColor: dataset.borderColor || color.main,
        pointBorderColor: isDark.value ? '#1f2937' : '#ffffff',
        pointBorderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 5,
        borderWidth: 2,
        tension: 0,
        fill: dataset.fill !== false,
      }
    })
  }
  
  return chartData
}

const createChart = async () => {
  // Wait for next tick to ensure canvas is mounted
  await nextTick()
  
  if (!chartCanvas.value) return
  
  // Destroy existing chart first
  destroyChart()
  
  const ctx = chartCanvas.value.getContext('2d')
  if (!ctx) return
  
  // Merge options
  const defaultOpts = getDefaultOptions()
  const mergedOptions = {
    ...defaultOpts,
    ...props.options,
    plugins: {
      ...defaultOpts.plugins,
      ...props.options.plugins,
    },
    scales: props.type !== 'doughnut' ? {
      ...defaultOpts.scales,
      ...props.options.scales,
      x: {
        ...defaultOpts.scales?.x,
        ...props.options.scales?.x,
        ticks: {
          ...defaultOpts.scales?.x?.ticks,
          ...props.options.scales?.x?.ticks,
        }
      },
      y: {
        ...defaultOpts.scales?.y,
        ...props.options.scales?.y,
        ticks: {
          ...defaultOpts.scales?.y?.ticks,
          ...props.options.scales?.y?.ticks,
        }
      }
    } : undefined,
  }
  
  // Process data
  const chartData = processChartData(ctx)
  
  try {
    chart = new Chart(ctx, {
      type: props.type,
      data: chartData,
      options: mergedOptions,
    })
  } catch (error) {
    console.warn('Failed to create chart:', error)
  }
}

const updateChart = async () => {
  if (!chart) {
    await createChart()
    return
  }
  
  if (!chartCanvas.value) return
  
  const ctx = chartCanvas.value.getContext('2d')
  if (!ctx) return
  
  const chartData = processChartData(ctx)
  
  try {
    // Update data
    chart.data.labels = chartData.labels
    chart.data.datasets = chartData.datasets
    
    // Update options for theme changes
    const defaultOpts = getDefaultOptions()
    chart.options = {
      ...defaultOpts,
      ...props.options,
      plugins: {
        ...defaultOpts.plugins,
        ...props.options.plugins,
      },
      scales: props.type !== 'doughnut' ? {
        ...defaultOpts.scales,
        ...props.options.scales,
        x: {
          ...defaultOpts.scales?.x,
          ...props.options.scales?.x,
          ticks: {
            ...defaultOpts.scales?.x?.ticks,
            ...props.options.scales?.x?.ticks,
          }
        },
        y: {
          ...defaultOpts.scales?.y,
          ...props.options.scales?.y,
          ticks: {
            ...defaultOpts.scales?.y?.ticks,
            ...props.options.scales?.y?.ticks,
          }
        }
      } : undefined,
    }
    
    chart.update('none')
  } catch (error) {
    console.warn('Failed to update chart, recreating:', error)
    await createChart()
  }
}

const destroyChart = () => {
  if (chart) {
    try {
      chart.destroy()
    } catch (error) {
      console.warn('Failed to destroy chart:', error)
    }
    chart = null
  }
}

// Watch for data changes - recreate chart to ensure proper gradient rendering
watch(() => props.data, async (newData, oldData) => {
  if (JSON.stringify(newData) !== JSON.stringify(oldData)) {
    await createChart()
  }
}, { deep: true })

// Watch for theme changes - recreate chart
watch(isDark, () => {
  createChart()
})

onMounted(() => {
  createChart()
})

onUnmounted(() => {
  destroyChart()
})
</script>
