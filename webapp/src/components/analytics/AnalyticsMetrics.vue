<template>
  <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
    <div class="card">
      <p class="metric-label">Avg Throughput</p>
      <p class="metric-value">{{ avgThroughput }}</p>
      <p class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">messages/sec</p>
    </div>
    
    <div class="card">
      <p class="metric-label">Total Processed</p>
      <p class="metric-value">{{ formatNumber(totalProcessed) }}</p>
      <p class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">in time range</p>
    </div>
    
    <div class="card">
      <p class="metric-label">Error Rate</p>
      <p class="metric-value">{{ errorRate }}%</p>
      <p class="text-xs text-gray-500 dark:text-gray-400 mt-1.5">failed / total</p>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';
import { formatNumber } from '../../utils/formatters';

const props = defineProps({
  data: Object,
  messages: Object,
});

const avgThroughput = computed(() => {
  if (!props.data?.throughput?.length) return '0.00';
  
  const throughput = props.data.throughput;
  const total = throughput.reduce((sum, t) => sum + (t.processedPerSecond || t.processed || 0), 0);
  const avg = total / throughput.length;
  
  return avg.toFixed(2);
});

const totalProcessed = computed(() => {
  return props.messages?.completed || 0;
});

const errorRate = computed(() => {
  const failed = props.messages?.failed || 0;
  const dlq = props.messages?.deadLetter || 0;
  const total = props.messages?.total || 0;
  
  if (total === 0) return '0.00';
  
  const rate = ((failed + dlq) / total) * 100;
  return rate.toFixed(2);
});
</script>

