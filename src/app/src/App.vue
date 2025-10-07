<template>
  <div class="app-container">
    <Toast position="top-right" />
    <ConfirmDialog />
    
    <!-- Sidebar -->
    <div class="sidebar" :class="{ collapsed: sidebarCollapsed }">
      <div class="sidebar-header">
        <div class="logo">
          <i class="pi pi-crown text-4xl text-purple-400"></i>
          <span v-if="!sidebarCollapsed" class="ml-3 text-xl font-bold">Queen</span>
        </div>
        <Button 
          icon="pi pi-bars" 
          class="p-button-text p-button-rounded"
          @click="sidebarCollapsed = !sidebarCollapsed"
        />
      </div>
      
      <nav class="sidebar-nav">
        <router-link
          v-for="route in visibleRoutes"
          :key="route.path"
          :to="route.path"
          class="nav-item"
          :class="{ active: $route.name === route.name }"
        >
          <i :class="route.meta.icon" class="nav-icon"></i>
          <span v-if="!sidebarCollapsed" class="nav-label">{{ route.meta.title }}</span>
        </router-link>
      </nav>
      
      <div class="sidebar-footer">
        <ConnectionStatus :collapsed="sidebarCollapsed" />
      </div>
    </div>
    
    <!-- Main Content -->
    <div class="main-content" :class="{ 'sidebar-collapsed': sidebarCollapsed }">
      <Header />
      <div class="content-wrapper">
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
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import Button from 'primevue/button'
import Toast from 'primevue/toast'
import ConfirmDialog from 'primevue/confirmdialog'
import Header from './components/layout/Header.vue'
import ConnectionStatus from './components/layout/ConnectionStatus.vue'
import { useWebSocket } from './composables/useWebSocket'

const router = useRouter()
const sidebarCollapsed = ref(false)

// Get visible routes for navigation
const visibleRoutes = computed(() => {
  return router.options.routes.filter(route => !route.meta?.hidden)
})

// Initialize WebSocket connection
const { connect } = useWebSocket()

onMounted(() => {
  // Apply dark mode class for PrimeVue theme
  document.documentElement.classList.add('dark-mode')
  connect()
})
</script>

<style scoped>
.app-container {
  display: flex;
  min-height: 100vh;
  background: var(--surface-0);
}

/* Sidebar Styles */
.sidebar {
  width: 250px;
  background: var(--surface-100);
  border-right: 1px solid var(--surface-300);
  display: flex;
  flex-direction: column;
  transition: width 0.3s ease;
  position: fixed;
  height: 100vh;
  z-index: 100;
}

.sidebar.collapsed {
  width: 70px;
}

.sidebar-header {
  padding: 1.5rem 1rem;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid var(--surface-300);
}

.logo {
  display: flex;
  align-items: center;
  color: white;
}

.sidebar-nav {
  flex: 1;
  padding: 1rem 0;
  overflow-y: auto;
}

.nav-item {
  display: flex;
  align-items: center;
  padding: 0.75rem 1rem;
  color: #a3a3a3;
  text-decoration: none;
  transition: all 0.2s;
  position: relative;
}

.nav-item:hover {
  background: var(--surface-200);
  color: white;
}

.nav-item.active {
  background: var(--surface-300);
  color: #a855f7;
}

.nav-item.active::before {
  content: '';
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: 3px;
  background: #a855f7;
}

.nav-icon {
  font-size: 1.25rem;
  min-width: 2rem;
}

.nav-label {
  margin-left: 0.5rem;
  white-space: nowrap;
}

.sidebar-footer {
  padding: 1rem;
  border-top: 1px solid var(--surface-300);
}

/* Main Content */
.main-content {
  flex: 1;
  margin-left: 250px;
  transition: margin-left 0.3s ease;
  display: flex;
  flex-direction: column;
}

.main-content.sidebar-collapsed {
  margin-left: 70px;
}

.content-wrapper {
  flex: 1;
  padding: 1.5rem;
  overflow-y: auto;
}

/* Transitions */
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
  .sidebar {
    transform: translateX(-100%);
  }
  
  .sidebar:not(.collapsed) {
    transform: translateX(0);
  }
  
  .main-content {
    margin-left: 0;
  }
}
</style>