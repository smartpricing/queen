<template>
  <div v-if="isOpen" class="fixed inset-0 z-50 overflow-y-auto">
    <div class="flex items-center justify-center min-h-screen px-4">
      <!-- Backdrop with blur -->
      <div 
        class="fixed inset-0 bg-black bg-opacity-50 backdrop-blur-sm transition-opacity animate-fade-in"
        @click="close"
      ></div>
      
      <!-- Modal -->
      <div class="relative bg-white dark:bg-slate-800 rounded-xl shadow-2xl max-w-2xl w-full p-6 animate-scale-in border border-gray-200 dark:border-gray-700">
        <div class="flex items-center justify-between mb-4">
          <h3 class="text-lg font-bold">Push Message to {{ queueName }}</h3>
          <button @click="close" class="text-gray-400 hover:text-gray-600">
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        
        <form @submit.prevent="pushMessage" class="space-y-4">
          <div>
            <label class="block text-sm font-medium mb-1">Partition</label>
            <input
              v-model="form.partition"
              type="text"
              placeholder="Default"
              class="input"
            />
          </div>
          
          <div>
            <label class="block text-sm font-medium mb-1">Payload (JSON) *</label>
            <textarea
              v-model="form.payload"
              rows="10"
              placeholder='{"key": "value", "message": "Hello World"}'
              class="input font-mono text-xs"
              required
            ></textarea>
            <p v-if="jsonError" class="text-xs text-red-600 mt-1">{{ jsonError }}</p>
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
                placeholder="0"
                class="input"
              />
            </div>
          </div>
          
          <div>
            <label class="block text-sm font-medium mb-1">Trace ID (optional)</label>
            <input
              v-model="form.traceId"
              type="text"
              placeholder="trace-id-for-distributed-tracing"
              class="input"
            />
          </div>
          
          <div v-if="error" class="text-sm text-red-600 dark:text-red-400">
            {{ error }}
          </div>
          
          <div class="flex gap-3 pt-2">
            <button type="submit" :disabled="loading || !!jsonError" class="btn btn-primary flex-1">
              <span v-if="loading">Pushing...</span>
              <span v-else>Push Message</span>
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
import { ref, watch, computed } from 'vue';
import { messagesApi } from '../../api/messages';

const props = defineProps({
  isOpen: Boolean,
  queueName: String,
});

const emit = defineEmits(['close', 'pushed']);

const loading = ref(false);
const error = ref(null);
const form = ref({
  partition: 'Default',
  payload: '',
  ttl: null,
  priority: null,
  traceId: '',
});

const jsonError = computed(() => {
  if (!form.value.payload) return null;
  
  try {
    JSON.parse(form.value.payload);
    return null;
  } catch (e) {
    return 'Invalid JSON format';
  }
});

watch(() => props.isOpen, (newVal) => {
  if (newVal) {
    resetForm();
  }
});

function resetForm() {
  form.value = {
    partition: 'Default',
    payload: '',
    ttl: null,
    priority: null,
    traceId: '',
  };
  error.value = null;
}

async function pushMessage() {
  loading.value = true;
  error.value = null;
  
  try {
    const payload = JSON.parse(form.value.payload);
    
    const item = {
      queue: props.queueName,
      partition: form.value.partition || 'Default',
      payload: payload,
    };
    
    if (form.value.ttl) item.ttl = form.value.ttl;
    if (form.value.priority) item.priority = form.value.priority;
    if (form.value.traceId) item.traceId = form.value.traceId;
    
    await messagesApi.pushMessages({ items: [item] });
    emit('pushed');
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

