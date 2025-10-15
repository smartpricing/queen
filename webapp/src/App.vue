<template>
  <div :class="{ 'dark': isDark }" class="min-h-screen">
    <AppLayout>
      <router-view />
    </AppLayout>
  </div>
</template>

<script setup>
import { provide } from 'vue';
import AppLayout from './components/layout/AppLayout.vue';
import { useTheme } from './composables/useTheme';

const { isDark, toggleTheme } = useTheme();

// Provide theme globally
provide('theme', { isDark, toggleTheme });
</script>

