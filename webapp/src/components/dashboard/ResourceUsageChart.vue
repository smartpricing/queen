<template>
  <div class="resource-chart">
    <!-- Metric Selector -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
      <button
        @click="selectedMetrics.cpu = !selectedMetrics.cpu"
        :class="[
          'metric-toggle',
          selectedMetrics.cpu ? 'metric-toggle-active-rose' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.cpu ? 'bg-rose-500' : 'bg-gray-400']"></div>
        CPU %
      </button>
      <button
        @click="selectedMetrics.memory = !selectedMetrics.memory"
        :class="[
          'metric-toggle',
          selectedMetrics.memory ? 'metric-toggle-active-purple' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.memory ? 'bg-purple-500' : 'bg-gray-400']"></div>
        Memory (MB)
      </button>
    </div>

    <!-- Chart -->
    <div class="chart-container">
      <Line v-if="chartData" :data="chartData" :options="chartOptions" />
      <div v-else class="flex items-center justify-center h-full text-gray-500 text-sm">
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
      borderColor: 'rgba(244, 63, 94, 1)',
      backgroundColor: 'rgba(244, 63, 94, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(244, 63, 94, 1)',
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
      borderColor: 'rgba(168, 85, 247, 1)',
      backgroundColor: 'rgba(168, 85, 247, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(168, 85, 247, 1)',
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
        color: '#9ca3af',
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
        color: 'rgba(244, 63, 94, 0.1)',
        drawBorder: false,
      },
      ticks: {
        color: 'rgba(244, 63, 94, 0.8)',
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
        color: 'rgba(244, 63, 94, 0.8)',
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
        color: 'rgba(168, 85, 247, 0.8)',
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
        color: 'rgba(168, 85, 247, 0.8)',
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
  height: 250px;
  position: relative;
}

.metric-toggle {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.375rem 0.75rem;
  border-radius: 0.5rem;
  font-size: 0.75rem;
  font-weight: 500;
  transition: all 0.2s ease;
  cursor: pointer;
}

.metric-toggle-active-rose {
  background: rgba(244, 63, 94, 0.1);
  color: #f43f5e;
  border: 1px solid rgba(244, 63, 94, 0.2);
}

.metric-toggle-active-purple {
  background: rgba(168, 85, 247, 0.1);
  color: #a855f7;
  border: 1px solid rgba(168, 85, 247, 0.2);
}

.metric-toggle-inactive {
  background: transparent;
  color: #9ca3af;
  border: 1px solid rgba(0, 0, 0, 0.1);
}

.dark .metric-toggle-inactive {
  border-color: rgba(255, 255, 255, 0.1);
}

.metric-toggle:hover {
  transform: translateY(-1px);
}

.metric-toggle-active-rose:hover {
  background: rgba(244, 63, 94, 0.15);
}

.metric-toggle-active-purple:hover {
  background: rgba(168, 85, 247, 0.15);
}

.metric-toggle-inactive:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .metric-toggle-inactive:hover {
  background: rgba(255, 255, 255, 0.05);
}

.metric-dot {
  width: 0.5rem;
  height: 0.5rem;
  border-radius: 50%;
}
</style>

