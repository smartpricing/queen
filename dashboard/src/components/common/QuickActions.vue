<template>
  <div class="flex flex-wrap items-center gap-2">
    <button
      v-for="action in actions"
      :key="action.id"
      @click="action.onClick"
      :disabled="action.disabled"
      :class="[
        'inline-flex items-center gap-2 px-4 py-2 rounded-xl text-sm font-semibold',
        'disabled:opacity-50 disabled:cursor-not-allowed',
        action.variant === 'primary' && 'bg-primary-600 hover:bg-primary-700 text-white shadow-lg shadow-primary-500/30',
        action.variant === 'secondary' && 'bg-white dark:bg-gray-800 hover:bg-gray-50 dark:hover:bg-gray-700 text-gray-700 dark:text-gray-200 border border-gray-300 dark:border-gray-700 shadow-sm',
        action.variant === 'danger' && 'bg-red-600 hover:bg-red-700 text-white shadow-lg shadow-red-500/30'
      ]"
      :title="action.title"
    >
      <component v-if="action.icon" :is="action.icon" class="w-4 h-4" />
      <span>{{ action.label }}</span>
      <kbd v-if="action.shortcut" class="hidden sm:inline-flex items-center px-1.5 py-0.5 text-[10px] font-mono bg-black/10 dark:bg-white/10 rounded">
        {{ action.shortcut }}
      </kbd>
    </button>
  </div>
</template>

<script setup>
defineProps({
  actions: {
    type: Array,
    required: true
  }
});
</script>

