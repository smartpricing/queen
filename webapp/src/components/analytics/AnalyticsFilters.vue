<template>
  <div>
    <div class="flex flex-col gap-4">
      <!-- First Row: Time Range and Quick Selectors -->
      <div class="flex flex-col sm:flex-row gap-3 items-start">
        <!-- Time Range Quick Selectors -->
        <div class="flex items-center gap-2 flex-wrap">
          <label class="text-sm font-medium text-gray-700 dark:text-gray-300 whitespace-nowrap">Time Range:</label>
          <button
            v-for="range in timeRanges"
            :key="range.value"
            @click="selectQuickRange(range.value)"
            :class="[
              'time-range-btn',
              timeRange === range.value && !customMode ? 'time-range-active' : 'time-range-inactive'
            ]"
          >
            {{ range.label }}
          </button>
          <button
            @click="toggleCustomMode"
            :class="[
              'time-range-btn',
              customMode ? 'time-range-active' : 'time-range-inactive'
            ]"
          >
            Custom
          </button>
        </div>
      </div>

      <!-- Custom Date/Time Range -->
      <div v-if="customMode" class="flex items-center gap-3 pt-2 border-t border-gray-200 dark:border-gray-700">
        <div class="flex items-center gap-2">
          <label class="text-sm font-medium text-gray-700 dark:text-gray-300">From:</label>
          <input
            :value="customFrom"
            @input="$emit('update:customFrom', $event.target.value)"
            type="datetime-local"
            class="datetime-input"
          />
        </div>
        <div class="flex items-center gap-2">
          <label class="text-sm font-medium text-gray-700 dark:text-gray-300">To:</label>
          <input
            :value="customTo"
            @input="$emit('update:customTo', $event.target.value)"
            type="datetime-local"
            class="datetime-input"
          />
        </div>
        <button
          @click="$emit('applyCustomRange')"
          class="px-4 py-2 bg-blue-500 hover:bg-blue-600 text-white text-sm font-medium rounded-lg transition-colors"
        >
          Apply
        </button>
      </div>

      <!-- Second Row: Filters -->
      <div class="flex flex-col sm:flex-row gap-3">
        <!-- Queue Filter -->
        <select
          :value="queue"
          @change="$emit('update:queue', $event.target.value)"
          class="input flex-1 sm:w-48"
        >
          <option value="">All Queues</option>
          <option v-for="q in queues" :key="q.id" :value="q.name">
            {{ q.name }}
          </option>
        </select>
        
        <!-- Namespace Filter -->
        <select
          :value="namespace"
          @change="$emit('update:namespace', $event.target.value)"
          class="input sm:w-40"
        >
          <option value="">All Namespaces</option>
          <option v-for="ns in namespaces" :key="ns.namespace" :value="ns.namespace">
            {{ ns.namespace }}
          </option>
        </select>
        
        <!-- Task Filter -->
        <select
          :value="task"
          @change="$emit('update:task', $event.target.value)"
          class="input sm:w-40"
        >
          <option value="">All Tasks</option>
          <option v-for="t in tasks" :key="t.task" :value="t.task">
            {{ t.task }}
          </option>
        </select>
        
        <!-- Clear Filters -->
        <button
          v-if="queue || namespace || task"
          @click="clearFilters"
          class="btn btn-secondary whitespace-nowrap"
        >
          Clear Filters
        </button>
      </div>
    </div>
  </div>
</template>


<script setup>
const props = defineProps({
  timeRange: String,
  customMode: Boolean,
  customFrom: String,
  customTo: String,
  queue: String,
  namespace: String,
  task: String,
  queues: Array,
  namespaces: Array,
  tasks: Array,
});

const emit = defineEmits([
  'update:timeRange',
  'update:customMode', 
  'update:customFrom',
  'update:customTo',
  'update:queue',
  'update:namespace',
  'update:task',
  'applyCustomRange'
]);

const timeRanges = [
  { label: '1h', value: '1h' },
  { label: '6h', value: '6h' },
  { label: '24h', value: '24h' },
  { label: '7d', value: '7d' },
];

function selectQuickRange(value) {
  emit('update:customMode', false);
  emit('update:timeRange', value);
}

function toggleCustomMode() {
  emit('update:customMode', !props.customMode);
}

function clearFilters() {
  emit('update:queue', '');
  emit('update:namespace', '');
  emit('update:task', '');
}
</script>

<style scoped>
.time-range-btn {
  padding: 0.5rem 0.875rem;
  border-radius: 0.5rem;
  font-size: 0.875rem;
  font-weight: 500;
  transition: all 0.2s ease;
  cursor: pointer;
}

.time-range-active {
  background: rgba(59, 130, 246, 0.1);
  color: #3b82f6;
  border: 1px solid rgba(59, 130, 246, 0.2);
}

.time-range-inactive {
  background: transparent;
  color: #9ca3af;
  border: 1px solid rgba(0, 0, 0, 0.1);
}

.dark .time-range-inactive {
  border-color: rgba(255, 255, 255, 0.1);
}

.time-range-btn:hover {
  transform: translateY(-1px);
}

.time-range-active:hover {
  background: rgba(59, 130, 246, 0.15);
}

.time-range-inactive:hover {
  background: rgba(0, 0, 0, 0.03);
}

.dark .time-range-inactive:hover {
  background: rgba(255, 255, 255, 0.05);
}

.datetime-input {
  padding: 0.5rem 0.75rem;
  border-radius: 0.5rem;
  border: 1px solid rgba(0, 0, 0, 0.1);
  font-size: 0.875rem;
  font-family: ui-monospace, monospace;
  background: white;
  color: #374151;
  transition: border-color 0.2s ease;
}

.datetime-input:focus {
  outline: none;
  border-color: #3b82f6;
  box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1);
}

.dark .datetime-input {
  background: rgba(31, 41, 55, 0.5);
  color: #e5e7eb;
  border-color: rgba(255, 255, 255, 0.1);
}

.dark .datetime-input:focus {
  border-color: #3b82f6;
  box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.2);
}
</style>

