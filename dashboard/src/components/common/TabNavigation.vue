<template>
  <div class="border-b border-gray-200 dark:border-gray-800">
    <nav class="flex space-x-1">
      <button
        v-for="tab in tabs"
        :key="tab.id"
        @click="$emit('update:modelValue', tab.id)"
        :class="[
          'px-4 py-3 text-sm font-semibold border-b-2 whitespace-nowrap',
          modelValue === tab.id
            ? 'border-primary-600 text-primary-600 dark:text-primary-400'
            : 'border-transparent text-gray-600 dark:text-gray-400 hover:text-gray-900 dark:hover:text-gray-200'
        ]"
      >
        {{ tab.label }}
        <span
          v-if="tab.badge !== undefined"
          :class="[
            'ml-2 px-2 py-0.5 rounded-full text-xs font-bold',
            modelValue === tab.id
              ? 'bg-primary-100 dark:bg-primary-900/30 text-primary-800 dark:text-primary-300'
              : 'bg-gray-200 dark:bg-gray-800 text-gray-700 dark:text-gray-400'
          ]"
        >
          {{ tab.badge }}
        </span>
      </button>
    </nav>
  </div>
</template>

<script setup>
defineProps({
  modelValue: {
    type: String,
    required: true
  },
  tabs: {
    type: Array,
    required: true
  }
});

defineEmits(['update:modelValue']);
</script>

