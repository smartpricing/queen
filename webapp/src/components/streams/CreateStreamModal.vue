<template>
  <div class="fixed inset-0 z-50 overflow-y-auto">
    <div class="flex items-center justify-center min-h-screen px-4">
      <!-- Backdrop with blur -->
      <div 
        class="fixed inset-0 bg-black bg-opacity-50 backdrop-blur-sm transition-opacity animate-fade-in"
        @click="close"
      ></div>
      
      <!-- Modal -->
      <div class="relative bg-white dark:bg-slate-800 rounded-xl shadow-2xl max-w-2xl w-full p-6 animate-scale-in border border-gray-200 dark:border-gray-700">
        <div class="flex items-center justify-between mb-4">
          <h3 class="text-lg font-bold">Create New Stream</h3>
          <button @click="close" class="text-gray-400 hover:text-gray-600">
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        
        <form @submit.prevent="createStream" class="space-y-4">
          <!-- Stream Name & Namespace -->
          <div class="grid grid-cols-2 gap-3">
            <div>
              <label class="block text-sm font-medium mb-1">Stream Name *</label>
              <input
                v-model="form.name"
                type="text"
                required
                placeholder="my-stream"
                class="input"
              />
            </div>
            
            <div>
              <label class="block text-sm font-medium mb-1">Namespace *</label>
              <input
                v-model="form.namespace"
                type="text"
                required
                placeholder="analytics"
                class="input"
              />
            </div>
          </div>
          
          <!-- Partitioned Toggle -->
          <div class="flex items-center justify-between p-3 bg-gray-50 dark:bg-gray-800/50 rounded-lg">
            <div>
              <div class="text-sm font-medium">Partitioned Stream</div>
              <div class="text-xs text-gray-500 dark:text-gray-400">
                Process each partition independently (default: global)
              </div>
            </div>
            <label class="relative inline-flex items-center cursor-pointer">
              <input 
                v-model="form.partitioned" 
                type="checkbox" 
                class="sr-only peer"
              >
              <div class="w-11 h-6 bg-gray-300 peer-focus:outline-none peer-focus:ring-4 peer-focus:ring-purple-300 dark:peer-focus:ring-purple-800 rounded-full peer dark:bg-gray-600 peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-gray-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all dark:border-gray-600 peer-checked:bg-purple-600"></div>
            </label>
          </div>
          
          <!-- Source Queues -->
          <div>
            <label class="block text-sm font-medium mb-1">Source Queue Names *</label>
            <div class="space-y-2">
              <div v-for="(queue, index) in form.sourceQueueNames" :key="index" class="flex gap-2">
                <input
                  v-model="form.sourceQueueNames[index]"
                  type="text"
                  required
                  placeholder="queue-name"
                  class="input flex-1"
                />
                <button
                  v-if="form.sourceQueueNames.length > 1"
                  type="button"
                  @click="removeSourceQueue(index)"
                  class="btn btn-secondary px-3"
                  title="Remove queue"
                >
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
                  </svg>
                </button>
              </div>
              <button
                type="button"
                @click="addSourceQueue"
                class="btn btn-secondary text-sm w-full"
              >
                + Add Source Queue
              </button>
            </div>
          </div>
          
          <!-- Window Configuration -->
          <div class="border-t border-gray-200 dark:border-gray-700 pt-4">
            <h4 class="text-sm font-semibold mb-3">Window Configuration</h4>
            
            <div class="grid grid-cols-2 gap-3">
              <div>
                <label class="block text-sm font-medium mb-1">Window Type</label>
                <select v-model="form.windowType" class="input">
                  <option value="tumbling">Tumbling</option>
                  <!-- Future: sliding, session -->
                </select>
              </div>
              
              <div>
                <label class="block text-sm font-medium mb-1">
                  Window Duration (ms) *
                </label>
                <input
                  v-model.number="form.windowDurationMs"
                  type="number"
                  required
                  min="1000"
                  placeholder="60000"
                  class="input"
                />
                <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  {{ formatDuration(form.windowDurationMs) }}
                </div>
              </div>
            </div>
            
            <div class="grid grid-cols-2 gap-3 mt-3">
              <div>
                <label class="block text-sm font-medium mb-1">
                  Grace Period (ms)
                </label>
                <input
                  v-model.number="form.windowGracePeriodMs"
                  type="number"
                  min="0"
                  placeholder="30000"
                  class="input"
                />
                <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  {{ formatDuration(form.windowGracePeriodMs) }}
                </div>
              </div>
              
              <div>
                <label class="block text-sm font-medium mb-1">
                  Lease Timeout (ms)
                </label>
                <input
                  v-model.number="form.windowLeaseTimeoutMs"
                  type="number"
                  min="1000"
                  placeholder="60000"
                  class="input"
                />
                <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  {{ formatDuration(form.windowLeaseTimeoutMs) }}
                </div>
              </div>
            </div>
          </div>
          
          <div v-if="error" class="text-sm text-red-600 dark:text-red-400 bg-red-50 dark:bg-red-900/20 p-3 rounded">
            {{ error }}
          </div>
          
          <div class="flex gap-3 pt-2">
            <button type="submit" :disabled="loading" class="btn btn-primary flex-1">
              <span v-if="loading">Creating Stream...</span>
              <span v-else>Create Stream</span>
            </button>
            <button type="button" @click="close" class="btn btn-secondary">
              Cancel
            </button>
          </div>
        </form>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue';
import { streamsApi } from '../../api/streams';

const emit = defineEmits(['close', 'created']);

const loading = ref(false);
const error = ref(null);
const form = ref({
  name: '',
  namespace: '',
  partitioned: false,
  windowType: 'tumbling',
  windowDurationMs: 60000,
  windowGracePeriodMs: 30000,
  windowLeaseTimeoutMs: 60000,
  sourceQueueNames: [''],
});

function addSourceQueue() {
  form.value.sourceQueueNames.push('');
}

function removeSourceQueue(index) {
  form.value.sourceQueueNames.splice(index, 1);
}

function formatDuration(ms) {
  if (!ms) return '0s';
  
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

function close() {
  emit('close');
}

async function createStream() {
  loading.value = true;
  error.value = null;
  
  try {
    // Filter out empty source queue names
    const sourceQueueNames = form.value.sourceQueueNames.filter(q => q.trim() !== '');
    
    if (sourceQueueNames.length === 0) {
      error.value = 'At least one source queue is required';
      loading.value = false;
      return;
    }
    
    const payload = {
      name: form.value.name,
      namespace: form.value.namespace,
      partitioned: form.value.partitioned,
      window_type: form.value.windowType,
      window_duration_ms: form.value.windowDurationMs,
      window_grace_period_ms: form.value.windowGracePeriodMs,
      window_lease_timeout_ms: form.value.windowLeaseTimeoutMs,
      source_queue_names: sourceQueueNames,
    };
    
    await streamsApi.defineStream(payload);
    emit('created');
  } catch (err) {
    error.value = err.response?.data?.error || err.message;
  } finally {
    loading.value = false;
  }
}
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
</style>

