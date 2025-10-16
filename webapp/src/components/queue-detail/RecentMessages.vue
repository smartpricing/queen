<template>
  <div class="messages-container">
    <h3 class="text-base font-semibold mb-4">Recent Messages</h3>
    
    <div class="table-container scrollbar-thin">
      <table class="table">
        <thead>
          <tr>
            <th>Transaction ID</th>
            <th class="hidden sm:table-cell">Partition</th>
            <th class="text-right">Created</th>
            <th class="text-right hidden md:table-cell">Status</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="message in messages"
            :key="message.id"
            class="cursor-pointer"
            @click="$emit('message-click', message)"
          >
            <td>
              <div class="font-mono text-xs">{{ message.transactionId?.substring(0, 16) }}...</div>
            </td>
            <td class="hidden sm:table-cell">
              <span class="text-xs">{{ message.partition }}</span>
            </td>
            <td class="text-right text-xs">{{ formatTime(message.createdAt) }}</td>
            <td class="text-right hidden md:table-cell">
              <StatusBadge v-if="message.status" :status="message.status" />
            </td>
          </tr>
        </tbody>
      </table>
      
      <div v-if="!messages?.length" class="text-center py-8 text-gray-500 text-sm">
        No recent messages
      </div>
    </div>
  </div>
</template>

<style scoped>
.messages-container {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
  transition: all 0.3s ease;
}

.dark .messages-container {
  background: #0a0d14;
}

.messages-container:hover {
  background: #fafafa;
}

.dark .messages-container:hover {
  background: #0d1117;
}
</style>

<script setup>
import { formatTime } from '../../utils/formatters';
import StatusBadge from '../common/StatusBadge.vue';

defineProps({
  messages: Array,
});

defineEmits(['message-click']);
</script>

