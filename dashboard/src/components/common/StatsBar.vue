<template>
  <div class="flex items-center gap-6 flex-wrap">
    <div
      v-for="stat in stats"
      :key="stat.label"
      class="flex items-center gap-2"
    >
      <span class="text-xs font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider">
        {{ stat.label }}
      </span>
      <span class="px-2.5 py-1 bg-gray-200 dark:bg-gray-800 rounded text-sm font-bold text-gray-900 dark:text-white">
        {{ stat.value }}
      </span>
    </div>
  </div>
</template>

<script setup>
defineProps({
  stats: {
    type: Array,
    required: true
  }
});
</script>

