<template>
  <div class="sidebar-wrapper" :class="{ collapsed: !visible, expanded: visible }">
    <div class="sidebar-content">
      <!-- Logo -->
      <div class="sidebar-logo">
        <div class="logo-icon">
          <i class="pi pi-bolt"></i>
        </div>
      </div>
      
      <!-- Navigation -->
      <nav class="sidebar-nav">
        <router-link 
          v-for="item in menuItems" 
          :key="item.path"
          :to="item.path"
          class="nav-item"
          :class="{ active: isActive(item.path) }"
          v-tooltip.right="!visible ? item.label : ''"
        >
          <div class="nav-icon">
            <i :class="item.icon"></i>
          </div>
        </router-link>
      </nav>
      
      <!-- Footer -->
      <div class="sidebar-footer">
        <div class="nav-item settings-item">
          <div class="nav-icon">
            <i class="pi pi-cog"></i>
          </div>
        </div>
        <div class="user-item">
          <div class="user-avatar">
            <i class="pi pi-user"></i>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRoute } from 'vue-router'

const props = defineProps({
  visible: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['update:visible'])
const route = useRoute()

const visible = computed({
  get: () => props.visible,
  set: (val) => emit('update:visible', val)
})

// Menu items - icon only sidebar
const menuItems = ref([
  {
    path: '/',
    label: 'Dashboard',
    icon: 'pi pi-home'
  },
  {
    path: '/queues',
    label: 'Queues',
    icon: 'pi pi-inbox'
  },
  {
    path: '/analytics',
    label: 'Analytics',
    icon: 'pi pi-chart-line'
  },
  {
    path: '/messages',
    label: 'Messages',
    icon: 'pi pi-envelope'
  },
])

// Check if route is active
const isActive = (path) => {
  if (path === '/') {
    return route.path === '/'
  }
  return route.path.startsWith(path)
}
</script>

<style scoped>
.sidebar-wrapper {
  position: fixed;
  left: 0;
  top: 0;
  width: 80px;
  height: 100vh;
  background: #0c0a09;
  border-right: 1px solid rgba(255, 255, 255, 0.05);
  transition: width 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  z-index: 100;
  display: flex;
  flex-direction: column;
}

.sidebar-wrapper.expanded {
  width: 250px;
}

.sidebar-content {
  height: 100%;
  display: flex;
  flex-direction: column;
  padding: 1rem 0;
}

/* Logo */
.sidebar-logo {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 1rem 0 2rem 0;
  margin-bottom: 1rem;
}

.logo-icon {
  width: 48px;
  height: 48px;
  border-radius: 12px;
  background: linear-gradient(135deg, #ec4899 0%, #db2777 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  font-size: 1.5rem;
  box-shadow: 0 4px 12px rgba(236, 72, 153, 0.3);
}

/* Navigation */
.sidebar-nav {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  padding: 0 1rem;
}

.nav-item {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 48px;
  height: 48px;
  margin: 0 auto;
  color: #78716c;
  text-decoration: none;
  transition: all 0.2s ease;
  position: relative;
  border-radius: 12px;
  cursor: pointer;
}

.nav-item:hover {
  color: #d6d3d1;
  background: rgba(236, 72, 153, 0.1);
}

.nav-item.active {
  color: #ec4899;
  background: rgba(236, 72, 153, 0.15);
}

.nav-item.active::before {
  content: '';
  position: absolute;
  left: -1rem;
  top: 50%;
  transform: translateY(-50%);
  width: 3px;
  height: 24px;
  background: #ec4899;
  border-radius: 0 2px 2px 0;
}

.nav-icon {
  font-size: 1.25rem;
  display: flex;
  align-items: center;
  justify-content: center;
}

/* Footer */
.sidebar-footer {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1rem;
  padding: 1rem 0;
  margin-top: auto;
}

.settings-item {
  margin: 0;
}

.user-item {
  display: flex;
  align-items: center;
  justify-content: center;
}

.user-avatar {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: #1c1917;
  border: 2px solid #292524;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #78716c;
  font-size: 1rem;
  cursor: pointer;
  transition: all 0.2s ease;
}

.user-avatar:hover {
  border-color: #ec4899;
  color: #ec4899;
}

/* Expanded state styles */
.sidebar-wrapper.expanded .nav-item {
  justify-content: flex-start;
  width: auto;
  padding: 0 1rem;
  gap: 1rem;
}

.sidebar-wrapper.expanded .nav-item::after {
  content: attr(data-label);
  font-size: 0.875rem;
  font-weight: 500;
}

/* Mobile responsive */
@media (max-width: 768px) {
  .sidebar-wrapper {
    transform: translateX(-100%);
  }
  
  .sidebar-wrapper.expanded {
    transform: translateX(0);
  }
}
</style>