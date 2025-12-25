<template>
  <div class="min-h-screen bg-light-100 dark:bg-dark-400 transition-colors duration-300 overflow-x-hidden">
    <!-- Gradient background overlay -->
    <div class="fixed inset-0 bg-mesh-light dark:bg-mesh-dark pointer-events-none" />
    
    <!-- Main layout -->
    <div class="relative flex">
      <!-- Sidebar -->
      <Sidebar />
      
      <!-- Main content area -->
      <main class="flex-1 lg:ml-64 min-w-0">
        <!-- Header -->
        <Header @refresh="handleRefresh" />
        
        <!-- Page content -->
        <div class="p-3 sm:p-4 lg:p-6">
          <router-view v-slot="{ Component }">
            <transition name="page" mode="out-in">
              <component :is="Component" ref="pageRef" />
            </transition>
          </router-view>
        </div>
      </main>
    </div>
  </div>
</template>

<script setup>
import { ref, provide } from 'vue'
import Sidebar from '@/components/Sidebar.vue'
import Header from '@/components/Header.vue'

const pageRef = ref(null)

// Refresh callback registry
const refreshCallbacks = ref(new Map())

const registerRefreshCallback = (key, callback) => {
  refreshCallbacks.value.set(key, callback)
}

const unregisterRefreshCallback = (key) => {
  refreshCallbacks.value.delete(key)
}

// Provide refresh registration to child components
provide('registerRefreshCallback', registerRefreshCallback)
provide('unregisterRefreshCallback', unregisterRefreshCallback)

// Handle refresh from header
const handleRefresh = () => {
  // Call all registered refresh callbacks
  refreshCallbacks.value.forEach((callback) => {
    if (typeof callback === 'function') {
      callback()
    }
  })
}
</script>

<style>
/* Page transitions */
.page-enter-active,
.page-leave-active {
  transition: opacity 0.2s ease, transform 0.2s ease;
}

.page-enter-from {
  opacity: 0;
  transform: translateY(10px);
}

.page-leave-to {
  opacity: 0;
  transform: translateY(-10px);
}
</style>

