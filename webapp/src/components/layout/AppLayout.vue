<template>
  <div class="flex h-screen bg-gray-50 dark:bg-slate-900 overflow-hidden">
    <!-- Mobile overlay -->
    <div 
      v-if="isSidebarOpen && isMobile"
      class="fixed inset-0 bg-black bg-opacity-50 z-20 lg:hidden"
      @click="closeSidebar"
    ></div>
    
    <!-- Sidebar -->
    <transition
      enter-active-class="transition-all duration-300"
      leave-active-class="transition-all duration-300"
      enter-from-class="-translate-x-full lg:translate-x-0"
      leave-to-class="-translate-x-full lg:translate-x-0"
    >
      <aside 
        v-show="isSidebarOpen || !isMobile"
        :class="[
          'fixed lg:relative inset-y-0 left-0 z-30 lg:z-0',
          isCollapsed && !isMobile ? 'w-16' : 'w-64',
          'bg-white dark:bg-slate-800 border-r',
          'transform transition-all duration-300',
          'flex flex-col'
        ]"
      >
        <AppSidebar 
          :is-collapsed="isCollapsed && !isMobile" 
          @close="closeSidebar"
          @toggle-collapse="toggleCollapse"
        />
      </aside>
    </transition>
    
    <!-- Main content -->
    <div class="flex-1 flex flex-col overflow-hidden min-w-0">
      <AppHeader 
        :is-sidebar-collapsed="isCollapsed"
        @toggle-sidebar="toggleSidebar"
      />
      
      <main class="flex-1 overflow-y-auto scrollbar-thin">
        <slot />
      </main>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, watch } from 'vue';
import { useRoute } from 'vue-router';
import AppHeader from './AppHeader.vue';
import AppSidebar from './AppSidebar.vue';

const route = useRoute();
const isSidebarOpen = ref(false);
const isCollapsed = ref(false);
const isMobile = ref(false);

function checkMobile() {
  isMobile.value = window.innerWidth < 1024;
  if (!isMobile.value) {
    isSidebarOpen.value = false;
  }
}

function toggleSidebar() {
  isSidebarOpen.value = !isSidebarOpen.value;
}

function toggleCollapse() {
  if (!isMobile.value) {
    isCollapsed.value = !isCollapsed.value;
    localStorage.setItem('sidebarCollapsed', isCollapsed.value ? 'true' : 'false');
  }
}

function closeSidebar() {
  if (isMobile.value) {
    isSidebarOpen.value = false;
  }
}

// Close mobile sidebar on route change
watch(() => route.path, () => {
  closeSidebar();
});

onMounted(() => {
  checkMobile();
  window.addEventListener('resize', checkMobile);
  
  // Restore collapsed state
  const savedState = localStorage.getItem('sidebarCollapsed');
  if (savedState === 'true') {
    isCollapsed.value = true;
  }
});

onUnmounted(() => {
  window.removeEventListener('resize', checkMobile);
});
</script>
