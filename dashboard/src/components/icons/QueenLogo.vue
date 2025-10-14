<template>
  <img :src="logoPath" alt="Queen Logo" :key="logoPath" />
</template>

<script setup>
import { computed } from 'vue';
import { useTheme } from '../../composables/useTheme';

// Beautiful Queen logo - a feminine duck with crown managing queues
// Using external SVG file from public folder for easier maintenance and reusability
// Logo automatically matches the active theme

const { currentTheme } = useTheme();

// Map theme names to logo file paths
const logoPath = computed(() => {
  // Map emerald to default logo, others to theme-specific logos
  if (currentTheme.value === 'emerald') {
    return '/queen-logo.svg';
  }
  
  // For other themes, use theme-specific logo
  return `/queen-logo-${currentTheme.value}.svg`;
});
</script>

<style scoped>
img {
  width: 100%;
  height: 100%;
  display: block;
}
</style>

