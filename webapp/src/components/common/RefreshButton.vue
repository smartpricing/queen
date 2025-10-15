<template>
  <button
    @click="$emit('refresh')"
    :disabled="loading"
    class="btn btn-secondary"
    aria-label="Refresh data"
  >
    <svg
      class="w-4 h-4 transition-transform"
      :class="{ 'animate-spin': loading }"
      fill="none"
      stroke="currentColor"
      viewBox="0 0 24 24"
    >
      <path
        stroke-linecap="round"
        stroke-linejoin="round"
        stroke-width="2"
        d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"
      />
    </svg>
    <span class="hidden sm:inline">Refresh</span>
  </button>
</template>

<script setup>
defineProps({
  loading: Boolean,
});

defineEmits(['refresh']);
</script>
