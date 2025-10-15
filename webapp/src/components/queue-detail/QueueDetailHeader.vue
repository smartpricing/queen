<template>
  <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
    <div>
      <div class="flex items-center gap-3 mb-2">
        <button @click="goBack" class="p-1 hover:bg-gray-100 dark:hover:bg-slate-700 rounded transition-colors">
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 19l-7-7m0 0l7-7m-7 7h18" />
          </svg>
        </button>
        <h2 class="text-xl font-bold">{{ queue?.name }}</h2>
      </div>
      <div class="flex items-center gap-3 text-sm text-gray-600 dark:text-gray-400 ml-9">
        <span>Namespace: {{ queue?.namespace || '-' }}</span>
        <span>Task: {{ queue?.task || '-' }}</span>
        <span>Priority: {{ queue?.priority || 0 }}</span>
        <span class="text-xs">Created: {{ formatDate(queue?.createdAt) }}</span>
      </div>
    </div>
    
    <div class="flex gap-2 ml-9 sm:ml-0">
      <button @click="$emit('push-message')" class="btn btn-primary">
        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4v16m8-8H4" />
        </svg>
        Push Message
      </button>
      <button @click="$emit('clear-queue')" class="btn btn-secondary">
        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
        </svg>
        Clear Queue
      </button>
    </div>
  </div>
</template>

<script setup>
import { useRouter } from 'vue-router';
import { formatDate } from '../../utils/formatters';

defineProps({
  queue: Object,
});

defineEmits(['push-message', 'clear-queue']);

const router = useRouter();

function goBack() {
  router.push('/queues');
}
</script>

