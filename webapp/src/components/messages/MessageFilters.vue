<template>
  <div>
    <div class="flex flex-col sm:flex-row gap-3">
      <!-- Search by Transaction ID -->
      <div class="flex-1">
        <input
          v-model="localSearch"
          @input="onSearchChange"
          type="text"
          placeholder="Search by transaction ID..."
          class="input font-mono text-xs"
        />
      </div>
      
      <!-- Queue Filter -->
      <select
        v-model="localQueue"
        @change="onQueueChange"
        class="input sm:w-48"
      >
        <option value="">All Queues</option>
        <option v-for="queue in queues" :key="queue.id" :value="queue.name">
          {{ queue.name }}
        </option>
      </select>
      
      <!-- Status Filter -->
      <select
        v-model="localStatus"
        @change="onStatusChange"
        class="input sm:w-40"
      >
        <option value="">All Status</option>
        <option value="pending">Pending</option>
        <option value="processing">Processing</option>
        <option value="completed">Completed</option>
        <option value="dead_letter">Dead Letter</option>
      </select>
      
      <!-- Clear Filters -->
      <button
        v-if="localSearch || localQueue || localStatus"
        @click="clearFilters"
        class="btn btn-secondary whitespace-nowrap"
      >
        Clear Filters
      </button>
    </div>
  </div>
</template>


<script setup>
import { ref, watch } from 'vue';

const props = defineProps({
  search: String,
  queue: String,
  status: String,
  queues: Array,
});

const emit = defineEmits(['update:search', 'update:queue', 'update:status']);

const localSearch = ref(props.search || '');
const localQueue = ref(props.queue || '');
const localStatus = ref(props.status || '');

watch(() => props.search, (newVal) => {
  localSearch.value = newVal || '';
});

watch(() => props.queue, (newVal) => {
  localQueue.value = newVal || '';
});

watch(() => props.status, (newVal) => {
  localStatus.value = newVal || '';
});

function onSearchChange() {
  emit('update:search', localSearch.value);
}

function onQueueChange() {
  emit('update:queue', localQueue.value);
}

function onStatusChange() {
  emit('update:status', localStatus.value);
}

function clearFilters() {
  localSearch.value = '';
  localQueue.value = '';
  localStatus.value = '';
  emit('update:search', '');
  emit('update:queue', '');
  emit('update:status', '');
}
</script>
