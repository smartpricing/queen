<template>
  <div class="fixed inset-0 z-50 overflow-y-auto">
    <div class="flex items-center justify-center min-h-screen px-4">
      <!-- Backdrop with blur -->
      <div 
        class="fixed inset-0 bg-black bg-opacity-50 backdrop-blur-sm transition-opacity animate-fade-in"
        @click="close"
      ></div>
      
      <!-- Modal -->
      <div class="relative bg-white dark:bg-slate-800 rounded-xl shadow-2xl max-w-4xl w-full p-6 animate-scale-in border border-gray-200 dark:border-gray-700 max-h-[90vh] overflow-y-auto">
        <div class="flex items-center justify-between mb-6">
          <div>
            <h3 class="text-xl font-bold text-gray-900 dark:text-gray-100">{{ stream.name }}</h3>
            <p class="text-sm text-gray-500 dark:text-gray-400 mt-1">{{ stream.namespace }}</p>
          </div>
          <button @click="close" class="text-gray-400 hover:text-gray-600 dark:hover:text-gray-300">
            <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        
        <LoadingSpinner v-if="loading" />
        
        <div v-else-if="error" class="text-sm text-red-600 dark:text-red-400 bg-red-50 dark:bg-red-900/20 p-4 rounded">
          {{ error }}
        </div>
        
        <div v-else class="space-y-6">
          <!-- Stream Configuration -->
          <div>
            <h4 class="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z" />
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
              </svg>
              Configuration
            </h4>
            <div class="grid grid-cols-2 md:grid-cols-3 gap-4">
              <div class="config-item">
                <div class="config-label">Type</div>
                <div class="config-value">
                  <span class="badge badge-purple">
                    {{ stream.partitioned ? 'Partitioned' : 'Global' }}
                  </span>
                </div>
              </div>
              <div class="config-item">
                <div class="config-label">Window Type</div>
                <div class="config-value">{{ stream.windowType }}</div>
              </div>
              <div class="config-item">
                <div class="config-label">Window Duration</div>
                <div class="config-value">{{ formatDuration(stream.windowDurationMs) }}</div>
              </div>
              <div class="config-item">
                <div class="config-label">Grace Period</div>
                <div class="config-value">{{ formatDuration(stream.windowGracePeriodMs) }}</div>
              </div>
              <div class="config-item">
                <div class="config-label">Lease Timeout</div>
                <div class="config-value">{{ formatDuration(stream.windowLeaseTimeoutMs) }}</div>
              </div>
              <div class="config-item">
                <div class="config-label">Created</div>
                <div class="config-value text-xs">{{ formatTimestamp(stream.createdAt) }}</div>
              </div>
            </div>
          </div>

          <!-- Source Queues -->
          <div>
            <h4 class="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6h16M4 10h16M4 14h16M4 18h16" />
              </svg>
              Source Queues ({{ stream.sourceQueues?.length || 0 }})
            </h4>
            <div class="flex flex-wrap gap-2">
              <span 
                v-for="queue in stream.sourceQueues" 
                :key="queue"
                class="badge badge-info"
              >
                {{ queue }}
              </span>
              <span v-if="!stream.sourceQueues || stream.sourceQueues.length === 0" class="text-sm text-gray-500">
                No source queues configured
              </span>
            </div>
          </div>

          <!-- Consumer Groups -->
          <div>
            <h4 class="text-sm font-semibold text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
              </svg>
              Consumer Groups ({{ consumers.length }})
            </h4>
            
            <div v-if="consumers.length > 0" class="overflow-x-auto">
              <table class="min-w-full divide-y divide-gray-200 dark:divide-gray-700">
                <thead>
                  <tr class="text-xs text-gray-500 dark:text-gray-400 uppercase tracking-wider">
                    <th class="px-3 py-2 text-left font-semibold">Group</th>
                    <th class="px-3 py-2 text-left font-semibold">Stream Key</th>
                    <th class="px-3 py-2 text-right font-semibold">Windows Consumed</th>
                    <th class="px-3 py-2 text-left font-semibold">Last Window End</th>
                    <th class="px-3 py-2 text-left font-semibold">Last Active</th>
                  </tr>
                </thead>
                <tbody class="divide-y divide-gray-200 dark:divide-gray-700">
                  <tr v-for="consumer in consumers" :key="`${consumer.consumerGroup}-${consumer.streamKey}`" class="text-sm">
                    <td class="px-3 py-2 font-medium text-gray-900 dark:text-gray-100">
                      {{ consumer.consumerGroup }}
                    </td>
                    <td class="px-3 py-2 text-gray-600 dark:text-gray-400">
                      <span class="font-mono text-xs">{{ consumer.streamKey }}</span>
                    </td>
                    <td class="px-3 py-2 text-right font-medium text-purple-600 dark:text-purple-400">
                      {{ formatNumber(consumer.totalWindowsConsumed) }}
                    </td>
                    <td class="px-3 py-2 text-gray-600 dark:text-gray-400">
                      <span v-if="consumer.lastAckedWindowEnd" class="font-mono text-xs">
                        {{ formatTimestamp(consumer.lastAckedWindowEnd) }}
                      </span>
                      <span v-else class="text-gray-400">-</span>
                    </td>
                    <td class="px-3 py-2 text-gray-600 dark:text-gray-400 text-xs">
                      <span v-if="consumer.lastConsumedAt">
                        {{ formatRelativeTime(consumer.lastConsumedAt) }}
                      </span>
                      <span v-else class="text-gray-400">Never</span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            
            <div v-else class="text-center py-8 text-gray-500 text-sm">
              <svg class="w-12 h-12 mx-auto mb-3 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z" />
              </svg>
              <p>No consumer groups yet</p>
              <p class="text-xs mt-1">Consumer groups will appear once they start consuming from this stream</p>
            </div>
          </div>

          <!-- Statistics -->
          <div class="grid grid-cols-2 md:grid-cols-4 gap-4 pt-4 border-t border-gray-200 dark:border-gray-700">
            <div class="text-center">
              <div class="text-2xl font-bold text-purple-600 dark:text-purple-400">
                {{ stream.activeLeases || 0 }}
              </div>
              <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">Active Windows</div>
            </div>
            <div class="text-center">
              <div class="text-2xl font-bold text-teal-600 dark:text-teal-400">
                {{ stream.consumerGroups || 0 }}
              </div>
              <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">Consumer Groups</div>
            </div>
            <div class="text-center">
              <div class="text-2xl font-bold text-orange-600 dark:text-pink-400">
                {{ formatNumber(totalWindowsConsumed) }}
              </div>
              <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">Windows Consumed</div>
            </div>
            <div class="text-center">
              <div class="text-2xl font-bold text-orange-600 dark:text-pink-400">
                {{ stream.sourceQueues?.length || 0 }}
              </div>
              <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">Source Queues</div>
            </div>
          </div>
        </div>
        
        <!-- Action Buttons -->
        <div class="flex gap-3 pt-6 border-t border-gray-200 dark:border-gray-700">
          <button @click="close" class="btn btn-secondary flex-1">
            Close
          </button>
          <button @click="confirmDelete" class="btn btn-danger">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
            </svg>
            Delete Stream
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { streamsApi } from '../../api/streams';
import { formatNumber } from '../../utils/formatters';
import LoadingSpinner from '../common/LoadingSpinner.vue';

const props = defineProps({
  stream: {
    type: Object,
    required: true
  }
});

const emit = defineEmits(['close', 'delete']);

const loading = ref(false);
const error = ref(null);
const consumers = ref([]);

const totalWindowsConsumed = computed(() => {
  return consumers.value.reduce((sum, c) => sum + (c.totalWindowsConsumed || 0), 0);
});

function formatDuration(ms) {
  if (!ms) return '-';
  
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  const days = Math.floor(hours / 24);
  
  if (days > 0) {
    const remHours = hours % 24;
    return remHours > 0 ? `${days}d ${remHours}h` : `${days}d`;
  }
  if (hours > 0) {
    const remMinutes = minutes % 60;
    return remMinutes > 0 ? `${hours}h ${remMinutes}m` : `${hours}h`;
  }
  if (minutes > 0) {
    const remSeconds = seconds % 60;
    return remSeconds > 0 ? `${minutes}m ${remSeconds}s` : `${minutes}m`;
  }
  return `${seconds}s`;
}

function formatTimestamp(timestamp) {
  if (!timestamp) return '-';
  const date = new Date(timestamp);
  return date.toLocaleString('en-US', {
    year: 'numeric',
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  });
}

function formatRelativeTime(timestamp) {
  if (!timestamp) return '-';
  const date = new Date(timestamp);
  const now = new Date();
  const diffMs = now - date;
  const diffSecs = Math.floor(diffMs / 1000);
  const diffMins = Math.floor(diffSecs / 60);
  const diffHours = Math.floor(diffMins / 60);
  const diffDays = Math.floor(diffHours / 24);
  
  if (diffSecs < 60) return `${diffSecs}s ago`;
  if (diffMins < 60) return `${diffMins}m ago`;
  if (diffHours < 24) return `${diffHours}h ago`;
  return `${diffDays}d ago`;
}

function close() {
  emit('close');
}

function confirmDelete() {
  emit('delete', props.stream);
}

async function loadConsumers() {
  loading.value = true;
  error.value = null;
  
  try {
    const response = await streamsApi.getStreamConsumers(props.stream.name);
    consumers.value = response.data.consumers || [];
  } catch (err) {
    error.value = err.message;
    console.error('Failed to load consumers:', err);
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadConsumers();
});
</script>

<style scoped>
.animate-fade-in {
  animation: fadeIn 0.2s ease-out;
}

.animate-scale-in {
  animation: scaleIn 0.2s ease-out;
}

@keyframes fadeIn {
  from {
    opacity: 0;
  }
  to {
    opacity: 1;
  }
}

@keyframes scaleIn {
  from {
    opacity: 0;
    transform: scale(0.95);
  }
  to {
    opacity: 1;
    transform: scale(1);
  }
}

.config-item {
  @apply bg-gray-50 dark:bg-gray-800/50 rounded-lg p-3;
}

.config-label {
  @apply text-xs font-medium text-gray-500 dark:text-gray-400 mb-1;
}

.config-value {
  @apply text-sm font-semibold text-gray-900 dark:text-gray-100;
}

.badge-purple {
  @apply bg-purple-100 text-purple-700 dark:bg-purple-900/30 dark:text-purple-400;
  @apply px-2 py-0.5 rounded text-xs font-medium;
}

.badge-info {
  @apply bg-orange-100 text-orange-700 dark:bg-orange-900/30 dark:text-pink-400;
  @apply px-2 py-0.5 rounded text-xs font-medium;
}

.btn-danger {
  @apply bg-red-600 hover:bg-red-700 text-white font-medium px-4 py-2 rounded-lg;
  @apply transition-colors duration-200;
  @apply flex items-center gap-2;
}
</style>

