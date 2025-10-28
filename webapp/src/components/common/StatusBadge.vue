<template>
  <span :class="badgeClass" class="flex items-center badge-glow">
    <span v-if="showDot" class="w-2 h-2 rounded-full mr-1.5" :class="dotClass"></span>
    {{ label }}
  </span>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  status: {
    type: String,
    required: true,
  },
  showDot: {
    type: Boolean,
    default: true,
  },
});

const statusConfig = {
  healthy: { class: 'badge-success', dot: 'bg-green-500', label: 'Healthy' },
  stable: { class: 'badge-success', dot: 'bg-green-500', label: 'Stable' },
  warning: { class: 'badge-warning', dot: 'bg-yellow-500', label: 'Warning' },
  error: { class: 'badge-danger', dot: 'bg-red-500', label: 'Error' },
  unhealthy: { class: 'badge-danger', dot: 'bg-red-500', label: 'Unhealthy' },
  pending: { class: 'badge-warning', dot: 'bg-yellow-500', label: 'Pending' },
  processing: { class: 'bg-blue-100 text-blue-800 dark:bg-blue-900/30 dark:text-blue-400', dot: 'bg-blue-500 animate-pulse', label: 'Processing' },
  completed: { class: 'badge-success', dot: 'bg-green-500', label: 'Completed' },
  failed: { class: 'badge-danger', dot: 'bg-red-500', label: 'Failed' },
  dead_letter: { class: 'bg-gray-100 text-gray-800 dark:bg-gray-700 dark:text-gray-300', dot: 'bg-gray-500', label: 'Dead Letter' },
};

const config = computed(() => statusConfig[props.status.toLowerCase()] || statusConfig.pending);
const badgeClass = computed(() => `badge ${config.value.class}`);
const dotClass = computed(() => config.value.dot);
const label = computed(() => config.value.label);
</script>

<style scoped>
.badge-glow {
  transition: all 0.2s ease;
}
</style>
