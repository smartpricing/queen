<template>
  <div class="flex h-screen overflow-hidden bg-gray-50 dark:bg-gray-950">
    <!-- Sidebar -->
    <AppSidebar :is-open="isSidebarOpen" @close="isSidebarOpen = false" @toggle-sidebar="isSidebarOpen = !isSidebarOpen" />
    
    <!-- Main content area -->
    <div class="flex flex-col flex-1 overflow-hidden">
      <!-- Page content -->
      <main class="flex-1 overflow-y-auto bg-gray-50 dark:bg-gray-950">
        <div class="container mx-auto px-4 py-4 max-w-screen-2xl">
          <slot />
        </div>
      </main>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue';
import AppSidebar from './AppSidebar.vue';

const isSidebarOpen = ref(false);
</script>

