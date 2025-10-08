<template>
  <div class="app-layout">
    <div class="layout-main">
      <AppSidebar v-model:visible="sidebarVisible" />
      <div class="layout-content">
        <router-view v-slot="{ Component }">
          <transition name="fade" mode="out-in">
            <component :is="Component" />
          </transition>
        </router-view>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, provide } from 'vue'
import AppSidebar from './AppSidebar.vue'

const sidebarVisible = ref(false)

// Provide sidebar visibility to header
provide('toggleSidebar', () => {
  sidebarVisible.value = !sidebarVisible.value
})
</script>

<style scoped>
.app-layout {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: var(--surface-0);
}

.layout-main {
  flex: 1;
  display: flex;
  position: relative;
}

.layout-content {
  flex: 1;
  padding: 2rem;
  margin-left: 80px;
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  background: #0c0a09;
  min-height: 100vh;
  position: relative;
}

.layout-main:has(.sidebar-wrapper.collapsed) .layout-content {
  margin-left: 80px;
}

.layout-main:has(.sidebar-wrapper.expanded) .layout-content {
  margin-left: 250px;
}

/* Fade transition */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

/* Responsive */
@media (max-width: 768px) {
  .layout-content {
    margin-left: 0 !important;
    padding: 0.75rem;
    background: var(--surface-0);
  }
  
  .layout-main:has(.sidebar-wrapper:not(.collapsed)) .layout-content {
    margin-left: 0 !important;
  }
}
</style>
