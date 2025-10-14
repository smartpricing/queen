<template>
  <div class="min-h-screen bg-gray-50 dark:bg-gray-950">
    <router-view />
  </div>
</template>

<script setup>
import { onMounted } from 'vue';
import { useColorMode } from './composables/useColorMode';

const { initColorMode } = useColorMode();

onMounted(() => {
  initColorMode();
});
</script>

