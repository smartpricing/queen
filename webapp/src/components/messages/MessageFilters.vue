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
            class="input"
          />
        </div>
        
        <!-- Queue Filter -->
        <div class="sm:w-48">
          <CustomSelect
            v-model="localQueue"
            @update:model-value="onQueueChange"
            :options="queueOptions"
            placeholder="All Queues"
            :clearable="true"
            :searchable="true"
          />
        </div>
        
        <!-- Status Filter -->
        <div class="sm:w-40">
          <CustomSelect
            v-model="localStatus"
            @update:model-value="onStatusChange"
            :options="statusOptions"
            placeholder="All Status"
            :clearable="true"
            :searchable="false"
          />
        </div>
      </div>
      
      <!-- Second Row: Date Range and Actions -->
      <div class="flex flex-col sm:flex-row items-start sm:items-center gap-3">
        <!-- Date From -->
        <div class="flex items-center gap-2 flex-1 w-full sm:w-auto">
          <label class="text-sm font-medium text-gray-700 dark:text-gray-300 whitespace-nowrap">From:</label>
          <DateTimePicker
            :model-value="localFromISO"
            @update:model-value="onFromChangeISO"
            placeholder="Select start date"
            :show-presets="true"
          />
        </div>
        
        <!-- Date To -->
        <div class="flex items-center gap-2 flex-1 w-full sm:w-auto">
          <label class="text-sm font-medium text-gray-700 dark:text-gray-300 whitespace-nowrap">To:</label>
          <DateTimePicker
            :model-value="localToISO"
            @update:model-value="onToChangeISO"
            placeholder="Select end date"
            :show-presets="true"
          />
        </div>
        
        <!-- Quick Time Range Buttons -->
        <div class="flex gap-2">
          <button @click="setTimeRange(1)" class="btn btn-secondary whitespace-nowrap">Last Hour</button>
          <button @click="setTimeRange(24)" class="btn btn-secondary whitespace-nowrap">Last 24h</button>
          <button @click="setTimeRange(168)" class="btn btn-secondary whitespace-nowrap">Last Week</button>
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
import CustomSelect from '../common/CustomSelect.vue';
import DateTimePicker from '../common/DateTimePicker.vue';

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
const localFromISO = ref('');
const localToISO = ref('');

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

// Options for CustomSelect components
const queueOptions = computed(() => [
  { value: '', label: 'All Queues' },
  ...(props.queues || []).map(q => ({ value: q.name, label: q.name }))
]);

const statusOptions = [
  { value: '', label: 'All Status' },
  { value: 'pending', label: 'Pending' },
  { value: 'processing', label: 'Processing' },
  { value: 'completed', label: 'Completed' },
  { value: 'dead_letter', label: 'Dead Letter' }
];

// Set default time range (last 1 hour) on mount
onMounted(() => {
  if (!props.from && !props.to) {
    setTimeRange(1);
  } else {
    if (props.from) {
      const fromDate = new Date(props.from);
      localFrom.value = formatDateTimeLocal(fromDate);
      localFromISO.value = props.from;
    }
    if (props.to) {
      const toDate = new Date(props.to);
      localTo.value = formatDateTimeLocal(toDate);
      localToISO.value = props.to;
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

function onQueueChange(value) {
  localQueue.value = value;
  emit('update:queue', value);
}

function onStatusChange(value) {
  localStatus.value = value;
  emit('update:status', value);
}

function onFromChange() {
  emit('update:from', toISOString(localFrom.value));
}

function onToChange() {
  emit('update:to', toISOString(localTo.value));
}

function onFromChangeISO(isoString) {
  localFromISO.value = isoString;
  if (isoString) {
    const date = new Date(isoString);
    localFrom.value = formatDateTimeLocal(date);
  }
  emit('update:from', isoString);
}

function onToChangeISO(isoString) {
  localToISO.value = isoString;
  if (isoString) {
    const date = new Date(isoString);
    localTo.value = formatDateTimeLocal(date);
  }
  emit('update:to', isoString);
}

function setTimeRange(hours) {
  const now = new Date();
  const from = new Date(now.getTime() - hours * 60 * 60 * 1000);
  
  localFrom.value = formatDateTimeLocal(from);
  localTo.value = formatDateTimeLocal(now);
  localFromISO.value = from.toISOString();
  localToISO.value = now.toISOString();
  
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
