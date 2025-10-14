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
    class: 'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-800 dark:text-yellow-300',
    label: 'Pending'
  },
  'processing': {
    class: 'bg-blue-100 dark:bg-blue-900/30 text-blue-800 dark:text-blue-300',
    label: 'Processing'
  },
  'completed': {
    class: 'bg-green-100 dark:bg-green-900/30 text-green-800 dark:text-green-300',
    label: 'Completed'
  },
  'failed': {
    class: 'bg-red-100 dark:bg-red-900/30 text-red-800 dark:text-red-300',
    label: 'Failed'
  },
  'active': {
    class: 'bg-emerald-100 dark:bg-emerald-900/30 text-emerald-800 dark:text-emerald-300',
    label: 'Active'
  },
  'inactive': {
    class: 'bg-gray-100 dark:bg-gray-800 text-gray-800 dark:text-gray-300',
    label: 'Inactive'
  }
};

const badgeClasses = computed(() => {
  const config = statusConfig[props.status] || statusConfig.inactive;
  return `inline-flex items-center px-2.5 py-0.5 rounded-full text-xs font-medium ${config.class}`;
});

const label = computed(() => {
  return props.label || statusConfig[props.status]?.label || props.status;
});
</script>

