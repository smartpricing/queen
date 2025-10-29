<template>
  <div
    v-if="isOpen"
    class="fixed inset-y-0 right-0 w-full sm:w-[32rem] bg-white dark:bg-slate-800 shadow-2xl z-40 overflow-y-auto scrollbar-thin transform transition-all duration-300 border-l border-gray-200 dark:border-gray-700"
    :class="isOpen ? 'translate-x-0' : 'translate-x-full'"
  >
    <!-- Backdrop for mobile -->
    <div
      v-if="isOpen"
      class="sm:hidden fixed inset-0 bg-black bg-opacity-50 backdrop-blur-sm -z-10 animate-fade-in"
      @click="close"
    ></div>
    
    <div class="p-6">
      <!-- Header -->
      <div class="flex items-start justify-between mb-6">
        <div class="flex-1 min-w-0">
          <h3 class="text-lg font-bold mb-1">Message Details</h3>
          <p class="text-xs font-mono text-gray-500 dark:text-gray-400 break-all">
            {{ message?.transactionId }}
          </p>
        </div>
        <button @click="close" class="text-gray-400 hover:text-gray-600 p-1">
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
          </svg>
        </button>
      </div>

      <LoadingSpinner v-if="loading" />

      <div v-else-if="error" class="text-sm text-red-600 dark:text-red-400">
        {{ error }}
      </div>

      <template v-else-if="messageDetail">
        <!-- Status Badge -->
        <div class="mb-6">
          <StatusBadge :status="messageDetail.status" />
        </div>

        <!-- Info Grid -->
        <div class="space-y-4 mb-6">
          <div>
            <label class="text-xs text-gray-500 dark:text-gray-400 block mb-1">Queue</label>
            <p class="text-sm font-medium">{{ messageDetail.queue }} / {{ messageDetail.partition }}</p>
          </div>
          
          <div>
            <label class="text-xs text-gray-500 dark:text-gray-400 block mb-1">Created</label>
            <p class="text-sm">{{ formatDate(messageDetail.createdAt) }}</p>
          </div>
          
          <div v-if="messageDetail.traceId">
            <label class="text-xs text-gray-500 dark:text-gray-400 block mb-1">Trace ID</label>
            <p class="text-xs font-mono break-all">{{ messageDetail.traceId }}</p>
          </div>
          
          <div v-if="messageDetail.errorMessage">
            <label class="text-xs text-gray-500 dark:text-gray-400 block mb-1">Error Message</label>
            <p class="text-sm text-red-600 dark:text-red-400">{{ messageDetail.errorMessage }}</p>
          </div>
          
          <div v-if="messageDetail.retryCount">
            <label class="text-xs text-gray-500 dark:text-gray-400 block mb-1">Retry Count</label>
            <p class="text-sm">{{ messageDetail.retryCount }}</p>
          </div>
        </div>

        <!-- Trace Events -->
        <div v-if="traceEvents && traceEvents.length > 0" class="mb-6">
          <h4 class="text-sm font-semibold mb-3 text-gray-700 dark:text-gray-300">
            Processing Timeline
          </h4>
          <div class="space-y-2">
            <div 
              v-for="trace in traceEvents" 
              :key="trace.id"
              class="bg-gray-50 dark:bg-slate-900 rounded-lg p-3 border-l-4"
              :class="getTraceColorClass(trace.event_type)"
            >
              <!-- Event header -->
              <div class="flex justify-between items-start mb-2">
                <span class="text-xs font-semibold uppercase tracking-wide">
                  {{ trace.event_type }}
                </span>
                <span class="text-xs text-gray-500">
                  {{ formatTime(trace.created_at) }}
                </span>
              </div>
              
              <!-- Trace names (if any) -->
              <div v-if="trace.trace_names && trace.trace_names.length > 0" class="mb-2">
                <div class="flex flex-wrap gap-1">
                  <span 
                    v-for="name in trace.trace_names"
                    :key="name"
                    class="inline-block px-2 py-0.5 text-xs rounded-full bg-blue-100 dark:bg-blue-900 text-blue-700 dark:text-blue-300"
                  >
                    {{ name }}
                  </span>
                </div>
              </div>
              
              <!-- User data -->
              <div class="text-sm text-gray-700 dark:text-gray-300">
                <div v-if="trace.data && trace.data.text">
                  {{ trace.data.text }}
                </div>
                
                <div v-if="hasAdditionalData(trace.data)" class="mt-2">
                  <div class="bg-white dark:bg-slate-800 rounded p-2 text-xs font-mono overflow-x-auto scrollbar-thin">
                    <pre>{{ formatTraceData(trace.data) }}</pre>
                  </div>
                </div>
              </div>
              
              <!-- Metadata -->
              <div class="flex gap-3 mt-2 text-xs text-gray-500">
                <span v-if="trace.worker_id">Worker: {{ trace.worker_id }}</span>
                <span v-if="trace.consumer_group && trace.consumer_group !== '__QUEUE_MODE__'">
                  Group: {{ trace.consumer_group }}
                </span>
              </div>
            </div>
          </div>
        </div>

        <!-- Payload -->
        <div class="mb-6">
          <label class="text-xs text-gray-500 dark:text-gray-400 block mb-2">Payload</label>
          <div class="bg-gray-50 dark:bg-slate-900 rounded-lg p-4 overflow-x-auto scrollbar-thin">
            <pre class="text-xs font-mono">{{ JSON.stringify(messageDetail.payload, null, 2) }}</pre>
          </div>
        </div>

        <!-- Actions -->
        <div class="space-y-2">
          <!-- Completed message info -->
          <div v-if="messageDetail.status === 'completed'" class="card bg-green-50 dark:bg-green-900/20 text-green-800 dark:text-green-200 text-sm">
            <div class="flex gap-2">
              <svg class="w-5 h-5 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <p>This message has been successfully consumed and acknowledged.</p>
            </div>
          </div>
          
          <button
            v-if="messageDetail.status === 'dead_letter'"
            @click="retryMessage"
            :disabled="actionLoading"
            class="btn btn-primary w-full"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
            Retry Message
          </button>
          
          <button
            v-if="messageDetail.status === 'pending'"
            @click="moveToDLQ"
            :disabled="actionLoading"
            class="btn btn-secondary w-full"
          >
            Move to Dead Letter Queue
          </button>
          
          <button
            @click="confirmDelete"
            :disabled="actionLoading"
            class="btn btn-danger w-full"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
            </svg>
            Delete Message
          </button>
        </div>

        <div v-if="actionError" class="text-sm text-red-600 dark:text-red-400 mt-4">
          {{ actionError }}
        </div>
      </template>
    </div>

    <!-- Delete Confirmation -->
    <ConfirmDialog
      :is-open="showDeleteConfirm"
      title="Delete Message"
      message="Are you sure you want to delete this message? This action cannot be undone."
      confirm-text="Delete"
      confirm-class="btn-danger"
      @confirm="deleteMessage"
      @cancel="showDeleteConfirm = false"
    />
  </div>
</template>

<script setup>
import { ref, watch } from 'vue';
import { messagesApi } from '../../api/messages';
import { formatDate } from '../../utils/formatters';

import StatusBadge from '../common/StatusBadge.vue';
import LoadingSpinner from '../common/LoadingSpinner.vue';
import ConfirmDialog from '../common/ConfirmDialog.vue';

const props = defineProps({
  isOpen: Boolean,
  message: Object,
});

const emit = defineEmits(['close', 'action-complete']);

const loading = ref(false);
const error = ref(null);
const actionLoading = ref(false);
const actionError = ref(null);
const messageDetail = ref(null);
const traceEvents = ref([]);
const showDeleteConfirm = ref(false);

watch(() => props.message, async (newMessage) => {
  if (newMessage) {
    await loadMessageDetail();
  }
});

async function loadMessageDetail() {
  if (!props.message?.transactionId || !props.message?.partitionId) return;
  
  loading.value = true;
  error.value = null;
  traceEvents.value = [];
  
  try {
    const response = await messagesApi.getMessage(props.message.partitionId, props.message.transactionId);
    messageDetail.value = response.data;
    
    // Load traces
    try {
      const traceResponse = await messagesApi.getTraces(props.message.partitionId, props.message.transactionId);
      traceEvents.value = traceResponse.data.traces || [];
    } catch (traceErr) {
      console.warn('Failed to load traces:', traceErr);
      // Don't fail the whole panel if traces can't be loaded
    }
  } catch (err) {
    error.value = err.response?.data?.error || err.message;
  } finally {
    loading.value = false;
  }
}

async function retryMessage() {
  actionLoading.value = true;
  actionError.value = null;
  
  try {
    await messagesApi.retryMessage(messageDetail.value.partitionId, messageDetail.value.transactionId);
    emit('action-complete');
    close();
  } catch (err) {
    actionError.value = err.response?.data?.error || err.message;
  } finally {
    actionLoading.value = false;
  }
}

async function moveToDLQ() {
  actionLoading.value = true;
  actionError.value = null;
  
  try {
    await messagesApi.moveToDLQ(messageDetail.value.partitionId, messageDetail.value.transactionId);
    emit('action-complete');
    close();
  } catch (err) {
    actionError.value = err.response?.data?.error || err.message;
  } finally {
    actionLoading.value = false;
  }
}

function confirmDelete() {
  showDeleteConfirm.value = true;
}

async function deleteMessage() {
  actionLoading.value = true;
  actionError.value = null;
  showDeleteConfirm.value = false;
  
  try {
    await messagesApi.deleteMessage(messageDetail.value.partitionId, messageDetail.value.transactionId);
    emit('action-complete');
    close();
  } catch (err) {
    actionError.value = err.response?.data?.error || err.message;
  } finally {
    actionLoading.value = false;
  }
}

function close() {
  emit('close');
  messageDetail.value = null;
  traceEvents.value = [];
}

function getTraceColorClass(eventType) {
  const colors = {
    info: 'border-blue-400',
    processing: 'border-green-400',
    step: 'border-purple-400',
    error: 'border-red-400',
    warning: 'border-yellow-400',
  };
  return colors[eventType] || 'border-gray-400';
}

function hasAdditionalData(data) {
  if (!data || typeof data !== 'object') return false;
  // Check if there's more than just 'text' field
  const keys = Object.keys(data).filter(k => k !== 'text');
  return keys.length > 0;
}

function formatTraceData(data) {
  if (!data || typeof data !== 'object') return '';
  // Remove 'text' field for display since we show it separately
  const { text, ...rest } = data;
  return JSON.stringify(rest, null, 2);
}

function formatTime(timestamp) {
  if (!timestamp) return '';
  const date = new Date(timestamp);
  return date.toLocaleTimeString('en-US', { 
    hour: '2-digit', 
    minute: '2-digit', 
    second: '2-digit',
    hour12: false 
  });
}
</script>

