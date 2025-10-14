<template>
  <span :class="badgeClasses">
    {{ label }}
  </span>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  status: {
    type: String,
    required: true
  },
  label: {
    type: String,
    default: null
  }
});

const statusConfig = {
  'pending': {
    class: 'bg-gradient-to-br from-amber-100 to-yellow-100 dark:from-yellow-900/30 dark:to-amber-900/30 text-yellow-800 dark:text-yellow-300 ring-1 ring-yellow-200 dark:ring-yellow-800',
    label: 'Pending'
  },
  'processing': {
    class: 'bg-gradient-to-br from-blue-100 to-cyan-100 dark:from-blue-900/30 dark:to-cyan-900/30 text-blue-800 dark:text-blue-300 ring-1 ring-blue-200 dark:ring-blue-800',
    label: 'Processing'
  },
  'completed': {
    class: 'bg-gradient-to-br from-primary-100 to-green-100 dark:from-primary-900/30 dark:to-green-900/30 text-primary-800 dark:text-primary-300 ring-1 ring-primary-200 dark:ring-primary-800',
    label: 'Completed'
  },
  'failed': {
    class: 'bg-gradient-to-br from-red-100 to-rose-100 dark:from-red-900/30 dark:to-rose-900/30 text-red-800 dark:text-red-300 ring-1 ring-red-200 dark:ring-red-800',
    label: 'Failed'
  },
  'active': {
    class: 'bg-gradient-to-br from-primary-100 to-primary-50 dark:from-primary-900/30 dark:to-primary-900/20 text-primary-800 dark:text-primary-300 ring-1 ring-primary-200 dark:ring-primary-800',
    label: 'Active'
  },
  'inactive': {
    class: 'bg-gray-100 dark:bg-gray-800 text-gray-700 dark:text-gray-300 ring-1 ring-gray-200 dark:ring-gray-700',
    label: 'Inactive'
  }
};

const badgeClasses = computed(() => {
  const config = statusConfig[props.status] || statusConfig.inactive;
  return `inline-flex items-center px-2 py-0.5 rounded-full text-xs font-semibold ${config.class}`;
});

const label = computed(() => {
  return props.label || statusConfig[props.status]?.label || props.status;
});
</script>

