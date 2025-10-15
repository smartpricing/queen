<template>
  <div class="config-container">
    <h3 class="text-base font-semibold mb-4">Queue Configuration</h3>
    <div class="grid grid-cols-2 md:grid-cols-3 gap-4 text-sm">
      <div>
        <span class="text-gray-500 dark:text-gray-400 block mb-1">Lease Time</span>
        <span class="font-semibold">{{ config?.leaseTime || 0 }}s</span>
      </div>
      <div>
        <span class="text-gray-500 dark:text-gray-400 block mb-1">TTL</span>
        <span class="font-semibold">{{ formatDuration((config?.ttl || 0) * 1000) }}</span>
      </div>
      <div>
        <span class="text-gray-500 dark:text-gray-400 block mb-1">Max Queue Size</span>
        <span class="font-semibold">{{ formatNumber(config?.maxQueueSize || 0) }}</span>
      </div>
      <div>
        <span class="text-gray-500 dark:text-gray-400 block mb-1">Retry Limit</span>
        <span class="font-semibold">{{ config?.retryLimit || 0 }}</span>
      </div>
      <div>
        <span class="text-gray-500 dark:text-gray-400 block mb-1">Retry Delay</span>
        <span class="font-semibold">{{ config?.retryDelay || 0 }}ms</span>
      </div>
      <div>
        <span class="text-gray-500 dark:text-gray-400 block mb-1">DLQ Enabled</span>
        <span class="font-semibold">{{ config?.deadLetterQueue ? 'Yes' : 'No' }}</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.config-container {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  transition: all 0.3s ease;
}

.dark .config-container {
  background: rgba(255, 255, 255, 0.03);
}

.config-container:hover {
  background: #fafafa;
}

.dark .config-container:hover {
  background: rgba(255, 255, 255, 0.05);
}
</style>

<script setup>
import { formatNumber, formatDuration } from '../../utils/formatters';

defineProps({
  config: Object,
});
</script>

