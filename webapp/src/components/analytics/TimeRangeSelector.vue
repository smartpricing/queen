<template>
  <div class="flex items-center gap-3">
    <label class="text-sm font-medium text-gray-700 dark:text-gray-300">Time Range:</label>
    <select
      :value="modelValue"
      @change="$emit('update:modelValue', $event.target.value)"
      class="input w-40"
    >
      <option value="1h">Last Hour</option>
      <option value="6h">Last 6 Hours</option>
      <option value="24h">Last 24 Hours</option>
      <option value="7d">Last 7 Days</option>
    </select>
  </div>
</template>

<script setup>
defineProps({
  modelValue: {
    type: String,
    default: '1h',
  },
});

defineEmits(['update:modelValue']);
</script>

