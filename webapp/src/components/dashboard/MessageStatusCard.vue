<template>
  <div>
    <div class="space-y-2.5">
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Pending</span>
        <span class="font-semibold">{{ formatNumber(calculatedPending) }}</span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Processing</span>
        <span class="font-semibold">{{ formatNumber(data?.processing || 0) }}</span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Completed</span>
        <span class="font-semibold text-green-600 dark:text-green-400">
          {{ formatNumber(data?.completed || 0) }}
        </span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Failed</span>
        <span class="font-semibold text-red-600 dark:text-red-400">
          {{ formatNumber(data?.failed || 0) }}
        </span>
      </div>
      <div class="flex items-center justify-between text-sm">
        <span class="text-gray-600 dark:text-gray-400">Dead Letter</span>
        <span class="font-semibold text-gray-600 dark:text-gray-400">
          {{ formatNumber(data?.deadLetter || 0) }}
        </span>
      </div>
      <div class="flex items-center justify-between text-sm pt-2 border-t border-gray-200 dark:border-gray-700">
        <span class="text-gray-600 dark:text-gray-400">Total in DB</span>
        <span class="font-semibold text-gray-500 dark:text-gray-400">
          {{ formatNumber(data?.total || 0) }}
        </span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { formatNumber } from '../../utils/formatters';

defineProps({
  data: Object,
  calculatedPending: {
    type: Number,
    default: 0,
  },
});
</script>
