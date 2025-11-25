<template>
  <div
    v-if="isOpen"
    ref="panelRef"
    class="fixed inset-y-0 right-0 bg-gray-50 dark:bg-[#0d1117] shadow-2xl z-40 overflow-y-auto scrollbar-thin transform transition-transform duration-300 border-l border-gray-200/60 dark:border-gray-800/60"
    :class="isOpen ? 'translate-x-0' : 'translate-x-full'"
    :style="{ width: panelWidth + 'px' }"
  >
    <!-- Resize handle -->
    <div
      ref="resizeHandle"
      @mousedown="startResize"
      class="absolute left-0 top-0 bottom-0 w-1 cursor-ew-resize hover:bg-rose-500 transition-colors group z-50"
    >
      <div class="absolute left-0 top-0 bottom-0 w-4 -ml-1.5"></div>
    </div>
    <!-- Backdrop for mobile -->
    <div
      v-if="isOpen"
      class="sm:hidden fixed inset-0 bg-black bg-opacity-50 backdrop-blur-sm -z-10 animate-fade-in"
      @click="close"
    ></div>
    
    <div class="p-5">
      <!-- Header -->
      <div class="flex items-start justify-between mb-5 pb-4 border-b border-gray-200/60 dark:border-gray-800/60">
        <div class="flex-1 min-w-0">
          <h3 class="text-base font-bold mb-1 text-gray-900 dark:text-white tracking-tight">Message Details</h3>
          <p class="text-xs font-mono text-gray-500 dark:text-gray-400 break-all">
            {{ message?.transactionId }}
          </p>
        </div>
        <button 
          @click="close" 
          class="text-gray-400 hover:text-gray-600 dark:hover:text-gray-300 p-1.5 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-800 transition-colors"
        >
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
        <!-- Status & Info -->
        <div class="mb-6">
          <div class="mb-5">
            <StatusBadge :status="messageDetail.status" />
          </div>

          <div class="space-y-4">
            <div>
              <label class="text-xs font-medium text-gray-500 dark:text-gray-400 block mb-1.5 uppercase tracking-wide">Queue</label>
              <p class="text-sm font-medium text-gray-900 dark:text-gray-100">{{ messageDetail.queue }} / {{ messageDetail.partition }}</p>
            </div>
            
            <div>
              <label class="text-xs font-medium text-gray-500 dark:text-gray-400 block mb-1.5 uppercase tracking-wide">Created</label>
              <p class="text-sm text-gray-700 dark:text-gray-300">{{ formatDate(messageDetail.createdAt) }}</p>
            </div>
            
            <div v-if="messageDetail.traceId">
              <label class="text-xs font-medium text-gray-500 dark:text-gray-400 block mb-1.5 uppercase tracking-wide">Trace ID</label>
              <p class="text-xs font-mono break-all text-gray-700 dark:text-gray-300">{{ messageDetail.traceId }}</p>
            </div>
            
            <div v-if="messageDetail.errorMessage">
              <label class="text-xs font-medium text-gray-500 dark:text-gray-400 block mb-1.5 uppercase tracking-wide">Error Message</label>
              <p class="text-sm text-red-600 dark:text-red-400">{{ messageDetail.errorMessage }}</p>
            </div>
            
            <div v-if="messageDetail.retryCount">
              <label class="text-xs font-medium text-gray-500 dark:text-gray-400 block mb-1.5 uppercase tracking-wide">Retry Count</label>
              <p class="text-sm text-gray-700 dark:text-gray-300">{{ messageDetail.retryCount }}</p>
            </div>
          </div>
        </div>

        <!-- Trace Events -->
        <div v-if="traceEvents && traceEvents.length > 0" class="mb-4">
          <h4 class="text-sm font-semibold mb-3 text-gray-900 dark:text-white tracking-tight">
            Processing Timeline
          </h4>
          <div class="table-container scrollbar-thin">
            <table class="table">
              <thead>
                <tr>
                  <th>Event</th>
                  <th>Time</th>
                  <th>Trace Names</th>
                  <th>Data</th>
                  <th class="hidden sm:table-cell">Worker / Group</th>
                </tr>
              </thead>
              <tbody>
                <tr 
                  v-for="trace in traceEvents" 
                  :key="trace.id"
                  class="hover:bg-gray-50 dark:hover:bg-gray-800/50"
                >
                  <!-- Event Type -->
                  <td>
                    <div class="flex items-center gap-2">
                      <div 
                        class="w-2.5 h-2.5 rounded-full flex-shrink-0"
                        :class="getEventColorDot(trace.event_type)"
                      ></div>
                      <span class="text-xs font-semibold uppercase tracking-wide text-gray-700 dark:text-gray-300">
                        {{ trace.event_type }}
                      </span>
                    </div>
                  </td>
                  
                  <!-- Time -->
                  <td>
                    <span class="text-xs text-gray-600 dark:text-gray-400 whitespace-nowrap">
                      {{ formatTime(trace.created_at) }}
                    </span>
                  </td>
                  
                  <!-- Trace Names -->
                  <td>
                    <div v-if="trace.trace_names && trace.trace_names.length > 0" class="flex flex-wrap gap-1">
                      <span 
                        v-for="name in trace.trace_names"
                        :key="name"
                        class="inline-block px-2 py-0.5 text-xs rounded-full whitespace-nowrap bg-orange-100 dark:bg-orange-900 text-orange-700 dark:text-emerald-300"
                      >
                        {{ name }}
                      </span>
                    </div>
                    <span v-else class="text-xs text-gray-400">-</span>
                  </td>
                  
                  <!-- Data -->
                  <td class="max-w-xs">
                    <div class="text-xs text-gray-700 dark:text-gray-300">
                      <div v-if="trace.data && trace.data.text" class="truncate" :title="trace.data.text">
                        {{ trace.data.text }}
                      </div>
                      <div v-else-if="hasAdditionalData(trace.data)" class="text-gray-500 dark:text-gray-400 italic">
                        JSON data
                      </div>
                      <span v-else class="text-gray-400">-</span>
                    </div>
                  </td>
                  
                  <!-- Worker / Group -->
                  <td class="hidden sm:table-cell">
                    <div class="text-xs text-gray-600 dark:text-gray-400 space-y-0.5">
                      <div v-if="trace.worker_id" class="truncate" :title="trace.worker_id">
                        {{ trace.worker_id }}
                      </div>
                      <div v-if="trace.consumer_group && trace.consumer_group !== '__QUEUE_MODE__'" class="text-gray-500 dark:text-gray-500">
                        {{ trace.consumer_group }}
                      </div>
                      <span v-if="!trace.worker_id && (!trace.consumer_group || trace.consumer_group === '__QUEUE_MODE__')" class="text-gray-400">-</span>
                    </div>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>

        <!-- Payload -->
        <div class="mb-4">
          <h4 class="text-sm font-semibold mb-3 text-gray-900 dark:text-white tracking-tight">Payload</h4>
          <JsonViewer :data="messageDetail.payload" />
        </div>

        <!-- Actions -->
        <div class="space-y-2 pt-2">
          <!-- Completed message info -->
          <div v-if="messageDetail.status === 'completed'" class="bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-700/30 rounded-lg p-3 text-green-800 dark:text-green-200 text-sm">
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
import { ref, watch, onMounted, onUnmounted } from 'vue';
import { messagesApi } from '../../api/messages';
import { formatDate } from '../../utils/formatters';

import StatusBadge from '../common/StatusBadge.vue';
import LoadingSpinner from '../common/LoadingSpinner.vue';
import ConfirmDialog from '../common/ConfirmDialog.vue';
import JsonViewer from '../common/JsonViewer.vue';

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

// Resizable panel state
const panelRef = ref(null);
const resizeHandle = ref(null);
const panelWidth = ref(512); // 32rem default
const isResizing = ref(false);
const minWidth = 320; // 20rem
const maxWidth = 1200; // 75rem

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
    info: 'border-pink-400',
    processing: 'border-green-400',
    step: 'border-purple-400',
    error: 'border-red-400',
    warning: 'border-yellow-400',
  };
  return colors[eventType] || 'border-gray-400';
}

function getEventColorDot(eventType) {
  const colors = {
    info: 'bg-orange-500',
    processing: 'bg-green-500',
    step: 'bg-purple-500',
    error: 'bg-red-500',
    warning: 'bg-yellow-500',
  };
  return colors[eventType] || 'bg-gray-500';
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

function getTraceDataWithoutText(data) {
  if (!data || typeof data !== 'object') return {};
  const { text, ...rest } = data;
  return rest;
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

// Resize functionality
function startResize(e) {
  isResizing.value = true;
  e.preventDefault();
  document.addEventListener('mousemove', handleResize);
  document.addEventListener('mouseup', stopResize);
  document.body.style.cursor = 'ew-resize';
  document.body.style.userSelect = 'none';
}

function handleResize(e) {
  if (!isResizing.value) return;
  
  const windowWidth = window.innerWidth;
  const newWidth = windowWidth - e.clientX;
  
  // Constrain width
  if (newWidth >= minWidth && newWidth <= maxWidth) {
    panelWidth.value = newWidth;
    // Save to localStorage
    localStorage.setItem('messagePanelWidth', newWidth.toString());
  }
}

function stopResize() {
  isResizing.value = false;
  document.removeEventListener('mousemove', handleResize);
  document.removeEventListener('mouseup', stopResize);
  document.body.style.cursor = '';
  document.body.style.userSelect = '';
}

onMounted(() => {
  // Restore saved width
  const savedWidth = localStorage.getItem('messagePanelWidth');
  if (savedWidth) {
    const width = parseInt(savedWidth);
    if (width >= minWidth && width <= maxWidth) {
      panelWidth.value = width;
    }
  }
});

onUnmounted(() => {
  // Clean up event listeners if component unmounts during resize
  if (isResizing.value) {
    stopResize();
  }
});
</script>

