<template>
  <div class="resource-chart">
    <!-- Metric Selector -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
      <button
        @click="selectedMetrics.cpu = !selectedMetrics.cpu"
        :class="[
          'metric-toggle',
          selectedMetrics.cpu ? 'metric-toggle-active-blue' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.cpu ? 'bg-emerald-500' : 'bg-gray-400']"></div>
        CPU %
      </button>
      <button
        @click="selectedMetrics.memory = !selectedMetrics.memory"
        :class="[
          'metric-toggle',
          selectedMetrics.memory ? 'metric-toggle-active-indigo' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.memory ? 'bg-indigo-500' : 'bg-gray-400']"></div>
        Memory (MB)
      </button>
    </div>

    <!-- Chart -->
    <div class="chart-container h-52">
      <Line v-if="chartData" :data="chartData" :options="chartOptions" />
      <div v-else class="flex items-center justify-center h-full text-gray-500 dark:text-gray-400 text-sm">
        No data available
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue';
import { Line } from 'vue-chartjs';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler,
} from 'chart.js';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  Filler
);

const props = defineProps({
  data: Object,
});

const selectedMetrics = ref({
  cpu: true,
  memory: true,
});

function getNestedValue(obj, path) {
  return path.split('.').reduce((acc, part) => acc?.[part], obj);
}

const chartData = computed(() => {
  if (!props.data?.replicas?.length) return null;

  const replicas = props.data.replicas;
  const datasets = [];

  // Get all unique timestamps
  const allTimestamps = new Set();
  replicas.forEach(replica => {
    replica.timeSeries?.forEach(point => {
      allTimestamps.add(point.timestamp);
    });
  });
  const sortedTimestamps = Array.from(allTimestamps).sort();

  // Aggregate data across all replicas for each timestamp
  const aggregatedData = {};
  
  sortedTimestamps.forEach(ts => {
    aggregatedData[ts] = { cpu: [], memory: [] };
    
    replicas.forEach(replica => {
      const point = replica.timeSeries?.find(p => p.timestamp === ts);
      if (point) {
        const cpuValue = getNestedValue(point.metrics, 'cpu.user_us')?.avg;
        const memValue = getNestedValue(point.metrics, 'memory.rss_bytes')?.avg;
        
        if (cpuValue !== undefined && cpuValue !== null) aggregatedData[ts].cpu.push(cpuValue);
        if (memValue !== undefined && memValue !== null) aggregatedData[ts].memory.push(memValue);
      }
    });
  });

  // CPU % dataset (averaged across replicas)
  if (selectedMetrics.value.cpu) {
    datasets.push({
      label: 'CPU %',
      data: sortedTimestamps.map(ts => {
        const values = aggregatedData[ts].cpu;
        if (values.length === 0) return null;
        const avg = values.reduce((sum, v) => sum + v, 0) / values.length;
        return avg / 100;
      }),
      borderColor: 'rgba(5, 150, 105, 1)',
      backgroundColor: 'rgba(5, 150, 105, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(5, 150, 105, 1)',
      yAxisID: 'y-cpu',
    });
  }

  // Memory MB dataset (averaged across replicas)
  if (selectedMetrics.value.memory) {
    datasets.push({
      label: 'Memory (MB)',
      data: sortedTimestamps.map(ts => {
        const values = aggregatedData[ts].memory;
        if (values.length === 0) return null;
        const avg = values.reduce((sum, v) => sum + v, 0) / values.length;
        return avg / 1024 / 1024;
      }),
      borderColor: 'rgba(99, 102, 241, 1)',
      backgroundColor: 'rgba(99, 102, 241, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(99, 102, 241, 1)',
      yAxisID: 'y-memory',
    });
  }

  return {
    labels: sortedTimestamps.map(ts => {
      const date = new Date(ts);
      return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
    }),
    datasets,
  };
});

const chartOptions = computed(() => {
  const scales = {
    x: {
      grid: {
        display: false,
      },
      ticks: {
        color: '#6b7280',
        font: {
          size: 11,
        },
        maxRotation: 0,
        autoSkipPadding: 20,
      },
    },
  };

  // CPU Y-axis (left)
  if (selectedMetrics.value.cpu) {
    scales['y-cpu'] = {
      type: 'linear',
      display: true,
      position: 'left',
      beginAtZero: true,
      grid: {
        color: 'rgba(156, 163, 175, 0.1)',
        drawBorder: false,
      },
      ticks: {
        color: '#6b7280',
        font: {
          size: 11,
        },
        callback: function(value) {
          return value.toFixed(1) + '%';
        },
      },
      title: {
        display: true,
        text: 'CPU %',
        color: '#6b7280',
        font: {
          size: 11,
          weight: 600,
        },
      },
    };
  }

  // Memory Y-axis (right)
  if (selectedMetrics.value.memory) {
    scales['y-memory'] = {
      type: 'linear',
      display: true,
      position: 'right',
      beginAtZero: true,
      grid: {
        display: false, // Don't show grid for right axis
        drawBorder: false,
      },
      ticks: {
        color: '#6b7280',
        font: {
          size: 11,
        },
        callback: function(value) {
          return value.toFixed(0) + ' MB';
        },
      },
      title: {
        display: true,
        text: 'Memory (MB)',
        color: '#6b7280',
        font: {
          size: 11,
          weight: 600,
        },
      },
    };
  }

  return {
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
        backgroundColor: 'rgba(0, 0, 0, 0.9)',
        padding: 12,
        titleColor: '#fff',
        bodyColor: '#fff',
        borderColor: 'rgba(255, 255, 255, 0.1)',
        borderWidth: 1,
        displayColors: true,
        callbacks: {
          label: function(context) {
            const value = context.parsed.y;
            if (context.dataset.label === 'CPU %') {
              return `CPU %: ${value.toFixed(1)}%`;
            } else if (context.dataset.label === 'Memory (MB)') {
              return `Memory (MB): ${value.toFixed(0)} MB`;
            }
            return `${context.dataset.label}: ${value}`;
          }
        }
      },
    },
    scales,
  };
});
</script>

<style scoped>
.resource-chart {
  width: 100%;
}

.chart-container {
  position: relative;
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
  background: rgba(5, 150, 105, 0.12);
  color: #2563eb;
  border: 1px solid rgba(5, 150, 105, 0.3);
  box-shadow: 0 1px 2px 0 rgba(5, 150, 105, 0.1);
}

.dark .metric-toggle-active-blue {
  background: rgba(5, 150, 105, 0.18);
  color: #10b981;
  border: 1px solid rgba(5, 150, 105, 0.4);
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
  background: rgba(5, 150, 105, 0.15);
}

.dark .metric-toggle-active-blue:hover {
  background: rgba(5, 150, 105, 0.2);
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

