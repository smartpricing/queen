<template>
  <div class="table-container scrollbar-thin">
    <table class="table">
      <thead>
        <tr>
          <th>Queue Name</th>
          <th class="text-right">Pending</th>
          <th class="text-right hidden sm:table-cell">Processing</th>
          <th class="text-right">Completed</th>
        </tr>
      </thead>
      <tbody>
        <tr
          v-for="queue in queues"
          :key="queue.id"
          class="cursor-pointer"
          @click="navigateToQueue(queue.name)"
        >
          <td>
            <div class="font-medium text-gray-900 dark:text-gray-100">{{ queue.name }}</div>
            <div v-if="queue.namespace" class="text-xs text-gray-500 dark:text-gray-400">
              {{ queue.namespace }}
            </div>
          </td>
          <td class="text-right font-medium">{{ formatNumber(queue.messages?.pending || 0) }}</td>
          <td class="text-right font-medium hidden sm:table-cell">{{ formatNumber(queue.messages?.processing || 0) }}</td>
          <td class="text-right font-medium text-green-600 dark:text-green-400">
            {{ formatNumber(queue.messages?.total || 0) }}
          </td>
        </tr>
      </tbody>
    </table>
    
    <div v-if="!queues?.length" class="text-center py-8 text-gray-500 text-sm">
      No queues available
    </div>
  </div>
</template>

<script setup>
import { useRouter } from 'vue-router';
import { formatNumber } from '../../utils/formatters';

defineProps({
  queues: Array,
});

const router = useRouter();

function navigateToQueue(queueName) {
  router.push(`/queues/${queueName}`);
}
</script>
