<template>
  <div class="queue-chart">
    <!-- Metric Selector -->
    <div class="flex items-center gap-2 flex-wrap mb-4">
      <button
        @click="selectedMetrics.dbActive = !selectedMetrics.dbActive"
        :class="[
          'metric-toggle',
          selectedMetrics.dbActive ? 'metric-toggle-active-blue' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.dbActive ? 'bg-blue-500' : 'bg-gray-400']"></div>
        DB Active
      </button>
      <button
        @click="selectedMetrics.dbQueue = !selectedMetrics.dbQueue"
        :class="[
          'metric-toggle',
          selectedMetrics.dbQueue ? 'metric-toggle-active-amber' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.dbQueue ? 'bg-amber-500' : 'bg-gray-400']"></div>
        DB Queue
      </button>
      <button
        @click="selectedMetrics.systemQueue = !selectedMetrics.systemQueue"
        :class="[
          'metric-toggle',
          selectedMetrics.systemQueue ? 'metric-toggle-active-green' : 'metric-toggle-inactive'
        ]"
      >
        <div :class="['metric-dot', selectedMetrics.systemQueue ? 'bg-green-500' : 'bg-gray-400']"></div>
        System Queue
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
  dbActive: true,
  dbQueue: true,
  systemQueue: false, // Default off since it's usually just 1
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
    aggregatedData[ts] = { dbActive: [], dbQueue: [], systemQueue: [] };
    
    replicas.forEach(replica => {
      const point = replica.timeSeries?.find(p => p.timestamp === ts);
      if (point) {
        const dbActiveValue = getNestedValue(point.metrics, 'database.pool_active')?.avg;
        const dbQueueValue = getNestedValue(point.metrics, 'threadpool.db.queue_size')?.avg;
        const sysQueueValue = getNestedValue(point.metrics, 'threadpool.system.queue_size')?.avg;
        
        if (dbActiveValue !== undefined && dbActiveValue !== null) aggregatedData[ts].dbActive.push(dbActiveValue);
        if (dbQueueValue !== undefined && dbQueueValue !== null) aggregatedData[ts].dbQueue.push(dbQueueValue);
        if (sysQueueValue !== undefined && sysQueueValue !== null) aggregatedData[ts].systemQueue.push(sysQueueValue);
      }
    });
  });

  // DB Active (summed across replicas - total active connections)
  if (selectedMetrics.value.dbActive) {
    datasets.push({
      label: 'DB Active',
      data: sortedTimestamps.map(ts => {
        const values = aggregatedData[ts].dbActive;
        if (values.length === 0) return null;
        return values.reduce((sum, v) => sum + v, 0); // SUM for total connections
      }),
      borderColor: 'rgba(59, 130, 246, 1)',
      backgroundColor: 'rgba(59, 130, 246, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(59, 130, 246, 1)',
    });
  }

  // DB Queue (summed across replicas - total queued tasks)
  if (selectedMetrics.value.dbQueue) {
    datasets.push({
      label: 'DB Queue',
      data: sortedTimestamps.map(ts => {
        const values = aggregatedData[ts].dbQueue;
        if (values.length === 0) return null;
        return values.reduce((sum, v) => sum + v, 0); // SUM for total queued
      }),
      borderColor: 'rgba(245, 158, 11, 1)',
      backgroundColor: 'rgba(245, 158, 11, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(245, 158, 11, 1)',
    });
  }

  // System Queue (summed across replicas)
  if (selectedMetrics.value.systemQueue) {
    datasets.push({
      label: 'System Queue',
      data: sortedTimestamps.map(ts => {
        const values = aggregatedData[ts].systemQueue;
        if (values.length === 0) return null;
        return values.reduce((sum, v) => sum + v, 0); // SUM for total queued
      }),
      borderColor: 'rgba(34, 197, 94, 1)',
      backgroundColor: 'rgba(34, 197, 94, 0.1)',
      borderWidth: 2,
      fill: true,
      tension: 0,
      pointRadius: 0,
      pointHoverRadius: 5,
      pointHoverBackgroundColor: 'rgba(34, 197, 94, 1)',
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

const chartOptions = {
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
          return `${context.dataset.label}: ${Math.round(value)}`;
        }
      }
    },
  },
  scales: {
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
    y: {
      type: 'linear',
      display: true,
      position: 'left',
      beginAtZero: true,
      grid: {
        color: 'rgba(0, 0, 0, 0.05)',
        drawBorder: false,
      },
      ticks: {
        color: '#9ca3af',
        font: {
          size: 11,
        },
        callback: function(value) {
          return Math.round(value);
        },
      },
    },
  },
};
</script>

<style scoped>
.queue-chart {
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

.metric-toggle-active-blue {
  background: rgba(59, 130, 246, 0.1);
  color: #3b82f6;
  border: 1px solid rgba(59, 130, 246, 0.2);
}

.metric-toggle-active-amber {
  background: rgba(245, 158, 11, 0.1);
  color: #f59e0b;
  border: 1px solid rgba(245, 158, 11, 0.2);
}

.metric-toggle-active-green {
  background: rgba(34, 197, 94, 0.1);
  color: #22c55e;
  border: 1px solid rgba(34, 197, 94, 0.2);
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

.metric-toggle-active-blue:hover {
  background: rgba(59, 130, 246, 0.15);
}

.metric-toggle-active-amber:hover {
  background: rgba(245, 158, 11, 0.15);
}

.metric-toggle-active-green:hover {
  background: rgba(34, 197, 94, 0.15);
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

