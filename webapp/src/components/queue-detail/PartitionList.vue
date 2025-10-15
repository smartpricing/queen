<template>
  <div class="card">
    <h3 class="text-base font-semibold mb-4">Partitions</h3>
    <div class="space-y-3">
      <div
        v-for="partition in partitions"
        :key="partition.id"
        class="p-4 bg-gray-50 dark:bg-slate-700/50 rounded-lg"
      >
        <div class="flex items-center justify-between mb-2">
          <h4 class="font-semibold">{{ partition.name }}</h4>
          <span class="text-xs text-gray-500">{{ formatDate(partition.createdAt) }}</span>
        </div>
        
        <div class="grid grid-cols-2 sm:grid-cols-4 gap-3 text-sm">
          <div>
            <span class="text-gray-500 dark:text-gray-400 block">Pending</span>
            <span class="font-semibold">{{ formatNumber(partition.messages?.pending || 0) }}</span>
          </div>
          <div>
            <span class="text-gray-500 dark:text-gray-400 block">Processing</span>
            <span class="font-semibold">{{ formatNumber(partition.messages?.processing || 0) }}</span>
          </div>
          <div>
            <span class="text-gray-500 dark:text-gray-400 block">Completed</span>
            <span class="font-semibold text-green-600">{{ formatNumber(partition.messages?.completed || 0) }}</span>
          </div>
          <div>
            <span class="text-gray-500 dark:text-gray-400 block">Failed</span>
            <span class="font-semibold text-red-600">{{ formatNumber(partition.messages?.failed || 0) }}</span>
          </div>
        </div>
        
        <div v-if="partition.cursor" class="mt-3 pt-3 border-t border-gray-200 dark:border-gray-600 text-xs text-gray-600 dark:text-gray-400">
          <div class="flex items-center justify-between">
            <span>Consumed: {{ formatNumber(partition.cursor.totalConsumed || 0) }} messages in {{ partition.cursor.batchesConsumed || 0 }} batches</span>
            <span v-if="partition.lastActivity">Last: {{ formatTime(partition.lastActivity) }}</span>
          </div>
        </div>
      </div>
    </div>
    
    <div v-if="!partitions?.length" class="text-center py-8 text-gray-500 text-sm">
      No partitions found
    </div>
  </div>
</template>

<script setup>
import { formatNumber, formatDate, formatTime } from '../../utils/formatters';

defineProps({
  partitions: Array,
});
</script>

