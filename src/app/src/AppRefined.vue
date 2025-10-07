<template>
  <div class="app-layout">
    <Toast position="top-right" />
    <ConfirmDialog />
    
    <!-- Refined Sidebar -->
    <nav class="sidebar" :class="{ collapsed: sidebarCollapsed }">
      <div class="sidebar-header">
        <router-link to="/" class="logo">
          <i class="pi pi-crown logo-icon"></i>
          <span v-if="!sidebarCollapsed" class="logo-text">Queen</span>
        </router-link>
        <button class="collapse-btn" @click="sidebarCollapsed = !sidebarCollapsed">
          <i :class="sidebarCollapsed ? 'pi pi-angle-right' : 'pi pi-angle-left'"></i>
        </button>
      </div>
      
      <div class="sidebar-nav">
        <router-link
          v-for="route in visibleRoutes"
          :key="route.path"
          :to="route.path"
          class="nav-link"
          :class="{ active: $route.name === route.name }"
          v-tooltip="{ value: sidebarCollapsed ? route.meta.title : null, position: 'right' }"
        >
          <i :class="route.meta.icon" class="nav-icon"></i>
          <span v-if="!sidebarCollapsed" class="nav-text">{{ route.meta.title }}</span>
        </router-link>
      </div>
      
      <div class="sidebar-footer">
        <ConnectionStatus :collapsed="sidebarCollapsed" />
      </div>
    </nav>
    
    <!-- Main Content -->
    <div class="main-container" :class="{ 'sidebar-collapsed': sidebarCollapsed }">
      <Header />
      <main class="content">
        <router-view v-slot="{ Component }">
          <transition name="fade" mode="out-in">
            <component :is="Component" />
          </transition>
        </router-view>
      </main>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
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
  connect()
  
  // Check saved sidebar state
  const saved = localStorage.getItem('sidebarCollapsed')
  if (saved) sidebarCollapsed.value = JSON.parse(saved)
})

// Save sidebar state
const toggleSidebar = () => {
  sidebarCollapsed.value = !sidebarCollapsed.value
  localStorage.setItem('sidebarCollapsed', JSON.stringify(sidebarCollapsed.value))
}
</script>

<style scoped>
.app-layout {
  display: flex;
  min-height: 100vh;
  background: var(--bg-primary);
}

/* Sidebar - Compact & Clean */
.sidebar {
  width: 220px;
  background: var(--bg-secondary);
  border-right: 1px solid var(--border-subtle);
  display: flex;
  flex-direction: column;
  transition: width var(--transition);
  position: fixed;
  height: 100vh;
  z-index: 100;
}

.sidebar.collapsed {
  width: 60px;
}

.sidebar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: var(--space-4);
  border-bottom: 1px solid var(--border-subtle);
}

.logo {
  display: flex;
  align-items: center;
  gap: var(--space-3);
  text-decoration: none;
  color: var(--text-primary);
}

.logo-icon {
  font-size: 1.25rem;
  color: var(--primary);
}

.logo-text {
  font-size: var(--text-lg);
  font-weight: 600;
  letter-spacing: -0.01em;
}

.collapse-btn {
  width: 24px;
  height: 24px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: transparent;
  border: none;
  color: var(--text-tertiary);
  cursor: pointer;
  border-radius: var(--radius-sm);
  transition: all var(--transition);
}

.collapse-btn:hover {
  background: var(--bg-tertiary);
  color: var(--text-secondary);
}

.sidebar-nav {
  flex: 1;
  padding: var(--space-3) var(--space-2);
  overflow-y: auto;
}

.nav-link {
  display: flex;
  align-items: center;
  gap: var(--space-3);
  padding: var(--space-2) var(--space-3);
  margin-bottom: var(--space-1);
  color: var(--text-secondary);
  text-decoration: none;
  border-radius: var(--radius-md);
  font-size: var(--text-sm);
  font-weight: 500;
  transition: all var(--transition);
}

.nav-link:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.nav-link.active {
  background: var(--primary-soft);
  color: var(--primary);
}

.nav-icon {
  font-size: 1rem;
  width: 20px;
  text-align: center;
}

.nav-text {
  white-space: nowrap;
}

.sidebar-footer {
  padding: var(--space-3);
  border-top: 1px solid var(--border-subtle);
}

/* Main Container */
.main-container {
  flex: 1;
  margin-left: 220px;
  transition: margin-left var(--transition);
  display: flex;
  flex-direction: column;
}

.main-container.sidebar-collapsed {
  margin-left: 60px;
}

.content {
  flex: 1;
  padding: var(--space-6);
  overflow-y: auto;
}

/* Transitions */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 150ms ease;
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
  
  .main-container {
    margin-left: 0;
  }
  
  .content {
    padding: var(--space-4);
  }
}
</style>
