<template>
  <div v-if="isOpen" class="fixed inset-0 z-50 overflow-y-auto">
    <div class="flex items-center justify-center min-h-screen px-4">
      <!-- Backdrop -->
      <div 
        class="fixed inset-0 bg-black bg-opacity-50 transition-opacity"
        @click="close"
      ></div>
      
      <!-- Modal -->
      <div class="relative bg-white dark:bg-slate-800 rounded-xl shadow-xl max-w-md w-full p-6">
        <div class="flex items-center justify-between mb-4">
          <h3 class="text-lg font-bold">Create New Queue</h3>
          <button @click="close" class="text-gray-400 hover:text-gray-600">
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        
        <form @submit.prevent="createQueue" class="space-y-4">
          <div>
            <label class="block text-sm font-medium mb-1">Queue Name *</label>
            <input
              v-model="form.queue"
              type="text"
              required
              placeholder="my-queue"
              class="input"
            />
          </div>
          
          <div>
            <label class="block text-sm font-medium mb-1">Partition</label>
            <input
              v-model="form.partition"
              type="text"
              placeholder="Default"
              class="input"
            />
          </div>
          
          <div class="grid grid-cols-2 gap-3">
            <div>
              <label class="block text-sm font-medium mb-1">TTL (seconds)</label>
              <input
                v-model.number="form.ttl"
                type="number"
                placeholder="3600"
                class="input"
              />
            </div>
            
            <div>
              <label class="block text-sm font-medium mb-1">Priority</label>
              <input
                v-model.number="form.priority"
                type="number"
                min="0"
                max="100"
                placeholder="0"
                class="input"
              />
            </div>
          </div>
          
          <div class="grid grid-cols-2 gap-3">
            <div>
              <label class="block text-sm font-medium mb-1">Namespace</label>
              <input
                v-model="form.namespace"
                type="text"
                placeholder="optional"
                class="input"
              />
            </div>
            
            <div>
              <label class="block text-sm font-medium mb-1">Task</label>
              <input
                v-model="form.task"
                type="text"
                placeholder="optional"
                class="input"
              />
            </div>
          </div>
          
          <div>
            <label class="block text-sm font-medium mb-1">Max Queue Size</label>
            <input
              v-model.number="form.maxQueueSize"
              type="number"
              placeholder="10000"
              class="input"
            />
          </div>
          
          <div v-if="error" class="text-sm text-red-600 dark:text-red-400">
            {{ error }}
          </div>
          
          <div class="flex gap-3 pt-2">
            <button type="submit" :disabled="loading" class="btn btn-primary flex-1">
              <span v-if="loading">Creating...</span>
              <span v-else>Create Queue</span>
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
import { ref, watch } from 'vue';
import { queuesApi } from '../../api/queues';

const props = defineProps({
  isOpen: Boolean,
});

const emit = defineEmits(['close', 'created']);

const loading = ref(false);
const error = ref(null);
const form = ref({
  queue: '',
  partition: 'Default',
  ttl: 3600,
  priority: 0,
  namespace: '',
  task: '',
  maxQueueSize: 10000,
});

watch(() => props.isOpen, (newVal) => {
  if (newVal) {
    resetForm();
  }
});

function resetForm() {
  form.value = {
    queue: '',
    partition: 'Default',
    ttl: 3600,
    priority: 0,
    namespace: '',
    task: '',
    maxQueueSize: 10000,
  };
  error.value = null;
}

async function createQueue() {
  loading.value = true;
  error.value = null;
  
  try {
    const payload = {
      queue: form.value.queue,
      partition: form.value.partition || 'Default',
      ttl: form.value.ttl,
      priority: form.value.priority,
      maxQueueSize: form.value.maxQueueSize,
    };
    
    if (form.value.namespace) {
      payload.namespace = form.value.namespace;
    }
    if (form.value.task) {
      payload.task = form.value.task;
    }
    
    await queuesApi.configureQueue(payload);
    emit('created');
    close();
  } catch (err) {
    error.value = err.response?.data?.error || err.message;
  } finally {
    loading.value = false;
  }
}

function close() {
  emit('close');
}
</script>

