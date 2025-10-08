<template>
  <div class="app-layout">
    <AppHeader />
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
import AppHeader from './AppHeader.vue'
import AppSidebar from './AppSidebar.vue'

const sidebarVisible = ref(true)

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
}

.layout-main {
  flex: 1;
  display: flex;
  position: relative;
}

.layout-content {
  flex: 1;
  padding: 1.5rem;
  margin-left: 250px;
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  background: linear-gradient(135deg, #f8f9fb 0%, #f1f3f5 100%);
  min-height: calc(100vh - 60px);
  position: relative;
}

.layout-main:has(.sidebar-wrapper.collapsed) .layout-content {
  margin-left: 0;
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
    background: #f5f6f8;
  }
  
  .layout-main:has(.sidebar-wrapper:not(.collapsed)) .layout-content {
    margin-left: 0 !important;
  }
}
</style>
