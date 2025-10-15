<template>
  <div class="filter-container">
    <div class="flex flex-col sm:flex-row gap-3">
      <!-- Time Range -->
      <div class="flex items-center gap-2">
        <label class="text-sm font-medium text-gray-700 dark:text-gray-300 whitespace-nowrap">Time Range:</label>
        <select
          :value="timeRange"
          @change="$emit('update:timeRange', $event.target.value)"
          class="input w-32"
        >
          <option value="1h">Last Hour</option>
          <option value="6h">6 Hours</option>
          <option value="24h">24 Hours</option>
          <option value="7d">7 Days</option>
        </select>
      </div>
      
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
</template>

<style scoped>
.filter-container {
  background: #ffffff;
  border: none;
  box-shadow: none;
  border-radius: 0.75rem;
  padding: 1rem;
}

.dark .filter-container {
  background: rgba(255, 255, 255, 0.03);
}
</style>

<script setup>
defineProps({
  timeRange: String,
  queue: String,
  namespace: String,
  task: String,
  queues: Array,
  namespaces: Array,
  tasks: Array,
});

const emit = defineEmits(['update:timeRange', 'update:queue', 'update:namespace', 'update:task']);

function clearFilters() {
  emit('update:queue', '');
  emit('update:namespace', '');
  emit('update:task', '');
}
</script>

