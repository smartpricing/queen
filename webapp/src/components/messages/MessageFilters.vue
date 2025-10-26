<template>
  <div>
    <div class="flex flex-col gap-3">
      <!-- First Row: Search and Basic Filters -->
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
      </div>
      
      <!-- Second Row: Date Range and Actions -->
      <div class="flex flex-col sm:flex-row gap-3 items-end">
        <!-- Date From -->
        <div class="flex-1">
          <label class="block text-xs text-gray-600 dark:text-gray-400 mb-1">From</label>
          <input
            v-model="localFrom"
            @change="onFromChange"
            type="datetime-local"
            class="input text-xs"
          />
        </div>
        
        <!-- Date To -->
        <div class="flex-1">
          <label class="block text-xs text-gray-600 dark:text-gray-400 mb-1">To</label>
          <input
            v-model="localTo"
            @change="onToChange"
            type="datetime-local"
            class="input text-xs"
          />
        </div>
        
        <!-- Quick Time Range Buttons -->
        <div class="flex gap-2">
          <button @click="setTimeRange(1)" class="btn btn-secondary text-xs whitespace-nowrap">Last Hour</button>
          <button @click="setTimeRange(24)" class="btn btn-secondary text-xs whitespace-nowrap">Last 24h</button>
          <button @click="setTimeRange(168)" class="btn btn-secondary text-xs whitespace-nowrap">Last Week</button>
        </div>
        
        <!-- Clear Filters -->
        <button
          v-if="hasActiveFilters"
          @click="clearFilters"
          class="btn btn-secondary whitespace-nowrap"
        >
          Clear All
        </button>
      </div>
    </div>
  </div>
</template>


<script setup>
import { ref, watch, computed, onMounted } from 'vue';

const props = defineProps({
  search: String,
  queue: String,
  status: String,
  from: String,
  to: String,
  queues: Array,
});

const emit = defineEmits(['update:search', 'update:queue', 'update:status', 'update:from', 'update:to']);

const localSearch = ref(props.search || '');
const localQueue = ref(props.queue || '');
const localStatus = ref(props.status || '');
const localFrom = ref('');
const localTo = ref('');

// Helper to format Date to datetime-local input format
function formatDateTimeLocal(date) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  return `${year}-${month}-${day}T${hours}:${minutes}`;
}

// Helper to convert datetime-local to ISO string
function toISOString(dateTimeLocal) {
  if (!dateTimeLocal) return '';
  return new Date(dateTimeLocal).toISOString();
}

// Set default time range (last 1 hour) on mount
onMounted(() => {
  if (!props.from && !props.to) {
    setTimeRange(1);
  } else {
    if (props.from) {
      const fromDate = new Date(props.from);
      localFrom.value = formatDateTimeLocal(fromDate);
    }
    if (props.to) {
      const toDate = new Date(props.to);
      localTo.value = formatDateTimeLocal(toDate);
    }
  }
});

const hasActiveFilters = computed(() => {
  return localSearch.value || localQueue.value || localStatus.value;
});

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

function onFromChange() {
  emit('update:from', toISOString(localFrom.value));
}

function onToChange() {
  emit('update:to', toISOString(localTo.value));
}

function setTimeRange(hours) {
  const now = new Date();
  const from = new Date(now.getTime() - hours * 60 * 60 * 1000);
  
  localFrom.value = formatDateTimeLocal(from);
  localTo.value = formatDateTimeLocal(now);
  
  emit('update:from', from.toISOString());
  emit('update:to', now.toISOString());
}

function clearFilters() {
  localSearch.value = '';
  localQueue.value = '';
  localStatus.value = '';
  
  // Reset to default last 1 hour
  setTimeRange(1);
  
  emit('update:search', '');
  emit('update:queue', '');
  emit('update:status', '');
}
</script>
