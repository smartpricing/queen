<template>
  <div>
    <div class="space-y-2.5">
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Requests/sec</span>
        <span class="font-semibold font-mono">{{ data?.requests?.rate?.toFixed(2) || '0.00' }}</span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Messages/sec</span>
        <span class="font-semibold font-mono">{{ data?.messages?.rate?.toFixed(2) || '0.00' }}</span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">DB Connections</span>
        <span class="font-semibold font-mono">
          {{ (data?.database?.poolSize || 0) - (data?.database?.idleConnections || 0) }}/{{ data?.database?.poolSize || 0 }}
        </span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Memory (Heap)</span>
        <span class="font-semibold font-mono">{{ formatBytes(data?.memory?.heapUsed || 0) }}</span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">CPU Time</span>
        <span class="font-semibold font-mono">
          {{ formatDuration(((data?.cpu?.user || 0) + (data?.cpu?.system || 0)) / 1000) }}
        </span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { formatBytes, formatDuration } from '../../utils/formatters';

defineProps({
  data: Object,
});
</script>
