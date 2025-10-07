<template>
  <div class="app-container">
    <Toast position="top-right" />
    <ConfirmDialog />
    
    <!-- Modern Sidebar with Glass Effect -->
    <aside class="sidebar" :class="{ collapsed: sidebarCollapsed }">
      <div class="sidebar-header">
        <div class="logo-container">
          <div class="logo-icon">
            <i class="pi pi-crown"></i>
          </div>
          <Transition name="fade">
            <div v-if="!sidebarCollapsed" class="logo-text">
              <span class="logo-title">Queen</span>
              <span class="logo-subtitle">Message Queue</span>
            </div>
          </Transition>
        </div>
        <button 
          class="sidebar-toggle"
          @click="sidebarCollapsed = !sidebarCollapsed"
        >
          <i :class="sidebarCollapsed ? 'pi pi-angle-right' : 'pi pi-angle-left'"></i>
        </button>
      </div>
      
      <nav class="sidebar-nav">
        <TransitionGroup name="nav-item">
          <router-link
            v-for="route in visibleRoutes"
            :key="route.path"
            :to="route.path"
            class="nav-item"
            :class="{ active: $route.name === route.name }"
            v-tooltip="{ value: sidebarCollapsed ? route.meta.title : null, position: 'right' }"
          >
            <div class="nav-item-content">
              <i :class="route.meta.icon" class="nav-icon"></i>
              <Transition name="fade">
                <span v-if="!sidebarCollapsed" class="nav-label">{{ route.meta.title }}</span>
              </Transition>
              <span v-if="!sidebarCollapsed && route.meta.badge" class="nav-badge">{{ route.meta.badge }}</span>
            </div>
          </router-link>
        </TransitionGroup>
      </nav>
      
      <div class="sidebar-footer">
        <ConnectionStatus :collapsed="sidebarCollapsed" />
        <Transition name="fade">
          <div v-if="!sidebarCollapsed" class="sidebar-version">
            <span>v1.0.0</span>
          </div>
        </Transition>
      </div>
    </aside>
    
    <!-- Main Content Area -->
    <main class="main-content" :class="{ 'sidebar-collapsed': sidebarCollapsed }">
      <Header />
      <div class="content-wrapper">
        <router-view v-slot="{ Component }">
          <Transition name="page" mode="out-in">
            <component :is="Component" />
          </Transition>
        </router-view>
      </div>
    </main>
    
    <!-- Floating Action Button -->
    <Transition name="fab">
      <button v-if="showFab" class="fab" @click="showQuickActions = true">
        <i class="pi pi-plus"></i>
      </button>
    </Transition>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import Toast from 'primevue/toast'
import ConfirmDialog from 'primevue/confirmdialog'
import Header from './components/layout/Header.vue'
import ConnectionStatus from './components/layout/ConnectionStatus.vue'
import { useWebSocket } from './composables/useWebSocket'

const router = useRouter()
const sidebarCollapsed = ref(false)
const showFab = ref(false)
const showQuickActions = ref(false)

// Get visible routes for navigation
const visibleRoutes = computed(() => {
  return router.options.routes.filter(route => !route.meta?.hidden)
})

// Initialize WebSocket connection
const { connect } = useWebSocket()

// Handle scroll for FAB visibility
const handleScroll = () => {
  showFab.value = window.scrollY > 100
}

onMounted(() => {
  // Apply dark mode class for PrimeVue theme
  document.documentElement.classList.add('dark-mode')
  connect()
  
  // Add scroll listener for FAB
  window.addEventListener('scroll', handleScroll)
  
  // Check localStorage for sidebar state
  const savedState = localStorage.getItem('sidebarCollapsed')
  if (savedState !== null) {
    sidebarCollapsed.value = JSON.parse(savedState)
  }
})

onUnmounted(() => {
  window.removeEventListener('scroll', handleScroll)
})

// Save sidebar state to localStorage
const toggleSidebar = () => {
  sidebarCollapsed.value = !sidebarCollapsed.value
  localStorage.setItem('sidebarCollapsed', JSON.stringify(sidebarCollapsed.value))
}
</script>

<style scoped>
.app-container {
  display: flex;
  min-height: 100vh;
  background: var(--surface-0);
  position: relative;
}

/* Modern Sidebar with Glass Effect */
.sidebar {
  width: 280px;
  background: var(--glass-bg);
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border-right: 1px solid var(--glass-border);
  display: flex;
  flex-direction: column;
  transition: width var(--transition-base);
  position: fixed;
  height: 100vh;
  z-index: 1000;
  box-shadow: 4px 0 24px rgba(0, 0, 0, 0.4);
}

.sidebar.collapsed {
  width: 80px;
}

/* Sidebar Header */
.sidebar-header {
  padding: 1.5rem;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid var(--glass-border);
  background: rgba(168, 85, 247, 0.05);
}

.logo-container {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.logo-icon {
  width: 45px;
  height: 45px;
  background: var(--gradient-primary);
  border-radius: var(--radius-lg);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.5rem;
  color: white;
  box-shadow: 0 4px 12px rgba(168, 85, 247, 0.3);
  transition: transform var(--transition-spring);
}

.logo-icon:hover {
  transform: rotate(15deg) scale(1.1);
}

.logo-text {
  display: flex;
  flex-direction: column;
}

.logo-title {
  font-size: 1.25rem;
  font-weight: 700;
  color: white;
  letter-spacing: -0.5px;
}

.logo-subtitle {
  font-size: 0.75rem;
  color: var(--neutral-400);
  text-transform: uppercase;
  letter-spacing: 1px;
}

.sidebar-toggle {
  width: 32px;
  height: 32px;
  border-radius: var(--radius-md);
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  color: var(--neutral-300);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all var(--transition-base);
}

.sidebar-toggle:hover {
  background: rgba(168, 85, 247, 0.1);
  border-color: rgba(168, 85, 247, 0.3);
  color: var(--primary-400);
  transform: translateX(2px);
}

/* Sidebar Navigation */
.sidebar-nav {
  flex: 1;
  padding: 1.5rem 0.75rem;
  overflow-y: auto;
  overflow-x: hidden;
}

.nav-item {
  display: block;
  margin-bottom: 0.5rem;
  text-decoration: none;
  border-radius: var(--radius-lg);
  transition: all var(--transition-base);
  position: relative;
  overflow: hidden;
}

.nav-item-content {
  display: flex;
  align-items: center;
  padding: 0.875rem 1rem;
  color: var(--neutral-300);
  position: relative;
  z-index: 1;
}

.nav-item::before {
  content: '';
  position: absolute;
  inset: 0;
  background: linear-gradient(135deg, transparent, rgba(168, 85, 247, 0.1));
  opacity: 0;
  transition: opacity var(--transition-base);
}

.nav-item:hover::before {
  opacity: 1;
}

.nav-item:hover .nav-item-content {
  color: white;
  transform: translateX(4px);
}

.nav-item.active {
  background: linear-gradient(135deg, rgba(168, 85, 247, 0.15), rgba(168, 85, 247, 0.05));
  border: 1px solid rgba(168, 85, 247, 0.2);
}

.nav-item.active .nav-item-content {
  color: var(--primary-400);
}

.nav-item.active::after {
  content: '';
  position: absolute;
  left: 0;
  top: 50%;
  transform: translateY(-50%);
  width: 3px;
  height: 24px;
  background: var(--gradient-primary);
  border-radius: 0 2px 2px 0;
  box-shadow: 0 0 12px rgba(168, 85, 247, 0.6);
}

.nav-icon {
  font-size: 1.25rem;
  min-width: 24px;
  margin-right: 1rem;
  transition: transform var(--transition-base);
}

.nav-item:hover .nav-icon {
  transform: scale(1.1);
}

.nav-label {
  flex: 1;
  white-space: nowrap;
  font-weight: 500;
  font-size: 0.9rem;
}

.nav-badge {
  padding: 0.125rem 0.5rem;
  background: var(--gradient-primary);
  color: white;
  border-radius: var(--radius-full);
  font-size: 0.75rem;
  font-weight: 600;
}

/* Sidebar Footer */
.sidebar-footer {
  padding: 1.5rem;
  border-top: 1px solid var(--glass-border);
  background: rgba(0, 0, 0, 0.2);
}

.sidebar-version {
  margin-top: 0.75rem;
  text-align: center;
  font-size: 0.75rem;
  color: var(--neutral-500);
}

/* Main Content */
.main-content {
  flex: 1;
  margin-left: 280px;
  transition: margin-left var(--transition-base);
  display: flex;
  flex-direction: column;
  min-height: 100vh;
}

.main-content.sidebar-collapsed {
  margin-left: 80px;
}

.content-wrapper {
  flex: 1;
  padding: 2rem;
  overflow-y: auto;
  animation: fade-in 0.5s ease-out;
}

/* Floating Action Button */
.fab {
  position: fixed;
  bottom: 2rem;
  right: 2rem;
  width: 56px;
  height: 56px;
  border-radius: var(--radius-full);
  background: var(--gradient-primary);
  border: none;
  color: white;
  font-size: 1.5rem;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  box-shadow: 0 4px 12px rgba(168, 85, 247, 0.4),
              0 8px 24px rgba(0, 0, 0, 0.3);
  transition: all var(--transition-spring);
  z-index: 999;
}

.fab:hover {
  transform: scale(1.1) rotate(90deg);
  box-shadow: 0 6px 20px rgba(168, 85, 247, 0.5),
              0 12px 32px rgba(0, 0, 0, 0.4);
}

.fab:active {
  transform: scale(0.95);
}

/* Animations */
.fade-enter-active,
.fade-leave-active {
  transition: opacity var(--transition-fast);
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

.page-enter-active,
.page-leave-active {
  transition: all var(--transition-base);
}

.page-enter-from {
  opacity: 0;
  transform: translateY(20px);
}

.page-leave-to {
  opacity: 0;
  transform: translateY(-20px);
}

.nav-item-enter-active,
.nav-item-leave-active {
  transition: all var(--transition-base);
}

.nav-item-enter-from,
.nav-item-leave-to {
  opacity: 0;
  transform: translateX(-20px);
}

.fab-enter-active,
.fab-leave-active {
  transition: all var(--transition-spring);
}

.fab-enter-from,
.fab-leave-to {
  opacity: 0;
  transform: scale(0) rotate(-180deg);
}

/* Responsive */
@media (max-width: 768px) {
  .sidebar {
    transform: translateX(-100%);
  }
  
  .sidebar:not(.collapsed) {
    transform: translateX(0);
    width: 100%;
    max-width: 280px;
  }
  
  .main-content {
    margin-left: 0;
  }
  
  .content-wrapper {
    padding: 1rem;
  }
  
  .fab {
    bottom: 1rem;
    right: 1rem;
  }
}
</style>