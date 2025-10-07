<template>
  <header class="app-header glass-header">
    <div class="header-left">
      <!-- Breadcrumb with modern style -->
      <nav class="breadcrumb">
        <router-link to="/" class="breadcrumb-item">
          <i class="pi pi-home"></i>
        </router-link>
        <i class="pi pi-angle-right breadcrumb-separator" v-if="currentRoute !== 'Dashboard'"></i>
        <span class="breadcrumb-item active" v-if="currentRoute !== 'Dashboard'">
          {{ pageTitle }}
        </span>
      </nav>
    </div>
    
    <div class="header-center">
      <!-- Global Search -->
      <div class="global-search">
        <i class="pi pi-search search-icon"></i>
        <input 
          type="text" 
          placeholder="Search queues, messages, or commands..." 
          class="search-input"
          v-model="searchQuery"
          @keyup.enter="performSearch"
        />
        <kbd class="search-shortcut">⌘K</kbd>
      </div>
    </div>
    
    <div class="header-right">
      <!-- Live Stats -->
      <div class="live-stats">
        <div class="stat-pill" v-tooltip="'Pending messages'">
          <div class="stat-icon pending">
            <i class="pi pi-clock"></i>
          </div>
          <span class="stat-value">{{ formatNumber(stats.pending) }}</span>
        </div>
        <div class="stat-pill" v-tooltip="'Processing messages'">
          <div class="stat-icon processing">
            <i class="pi pi-spin pi-spinner"></i>
          </div>
          <span class="stat-value">{{ formatNumber(stats.processing) }}</span>
        </div>
        <div class="stat-pill" v-tooltip="'Completed today'">
          <div class="stat-icon completed">
            <i class="pi pi-check"></i>
          </div>
          <span class="stat-value">{{ formatNumber(stats.completed) }}</span>
        </div>
      </div>
      
      <!-- Action Buttons -->
      <div class="header-actions">
        <button class="header-btn" @click="refresh" v-tooltip="'Refresh data'">
          <i class="pi pi-refresh" :class="{ 'pi-spin': isRefreshing }"></i>
        </button>
        
        <div class="notification-wrapper">
          <button class="header-btn" @click="toggleNotifications" v-tooltip="'Notifications'">
            <i class="pi pi-bell"></i>
            <span class="notification-badge" v-if="unreadCount > 0">{{ unreadCount }}</span>
          </button>
          
          <!-- Notifications Dropdown -->
          <Transition name="dropdown">
            <div v-if="showNotifications" class="notifications-dropdown glass-card">
              <div class="dropdown-header">
                <h4>Notifications</h4>
                <button class="clear-btn" @click="clearNotifications">Clear all</button>
              </div>
              <div class="notifications-list">
                <div v-for="notification in notifications" :key="notification.id" 
                     class="notification-item" :class="{ unread: !notification.read }">
                  <div class="notification-icon" :class="`type-${notification.type}`">
                    <i :class="getNotificationIcon(notification.type)"></i>
                  </div>
                  <div class="notification-content">
                    <p class="notification-title">{{ notification.title }}</p>
                    <p class="notification-message">{{ notification.message }}</p>
                    <span class="notification-time">{{ formatTime(notification.timestamp) }}</span>
                  </div>
                </div>
                <div v-if="notifications.length === 0" class="empty-notifications">
                  <i class="pi pi-inbox"></i>
                  <p>No notifications</p>
                </div>
              </div>
            </div>
          </Transition>
        </div>
        
        <button class="header-btn" @click="toggleTheme" v-tooltip="'Toggle theme'">
          <i :class="isDarkMode ? 'pi pi-sun' : 'pi pi-moon'"></i>
        </button>
        
        <!-- User Menu -->
        <div class="user-menu">
          <button class="user-avatar" @click="toggleUserMenu">
            <img src="https://ui-avatars.com/api/?name=Admin&background=667eea&color=fff" alt="User" />
            <span class="user-name">Admin</span>
            <i class="pi pi-chevron-down"></i>
          </button>
          
          <!-- User Dropdown -->
          <Transition name="dropdown">
            <div v-if="showUserMenu" class="user-dropdown glass-card">
              <router-link to="/profile" class="dropdown-item">
                <i class="pi pi-user"></i>
                <span>Profile</span>
              </router-link>
              <div class="dropdown-divider"></div>
              <button class="dropdown-item" @click="logout">
                <i class="pi pi-sign-out"></i>
                <span>Logout</span>
              </button>
            </div>
          </Transition>
        </div>
      </div>
    </div>
  </header>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useQueuesStore } from '../../stores/queues'
import { formatDistanceToNow } from 'date-fns'

const route = useRoute()
const router = useRouter()
const queuesStore = useQueuesStore()

// State
const searchQuery = ref('')
const showNotifications = ref(false)
const showUserMenu = ref(false)
const isRefreshing = ref(false)
const isDarkMode = ref(true)

// Mock notifications
const notifications = ref([
  {
    id: 1,
    type: 'success',
    title: 'Queue Processed',
    message: 'Email queue completed 1,234 messages',
    timestamp: new Date(Date.now() - 300000),
    read: false
  },
  {
    id: 2,
    type: 'warning',
    title: 'High Queue Depth',
    message: 'Batch processing queue has 5,000+ pending',
    timestamp: new Date(Date.now() - 600000),
    read: false
  },
  {
    id: 3,
    type: 'error',
    title: 'Processing Failed',
    message: '12 messages failed in payment queue',
    timestamp: new Date(Date.now() - 900000),
    read: true
  }
])

// Computed
const currentRoute = computed(() => route.name)
const pageTitle = computed(() => route.meta?.title || 'Dashboard')
const stats = computed(() => queuesStore.globalStats)
const unreadCount = computed(() => notifications.value.filter(n => !n.read).length)

// Methods
function formatNumber(num) {
  if (num >= 1000000) return `${(num / 1000000).toFixed(1)}M`
  if (num >= 1000) return `${(num / 1000).toFixed(1)}K`
  return num?.toString() || '0'
}

function formatTime(timestamp) {
  return formatDistanceToNow(timestamp, { addSuffix: true })
}

function getNotificationIcon(type) {
  const icons = {
    success: 'pi pi-check-circle',
    warning: 'pi pi-exclamation-triangle',
    error: 'pi pi-times-circle',
    info: 'pi pi-info-circle'
  }
  return icons[type] || 'pi pi-bell'
}

async function refresh() {
  isRefreshing.value = true
  await queuesStore.fetchQueues()
  setTimeout(() => {
    isRefreshing.value = false
  }, 500)
}

function performSearch() {
  if (searchQuery.value) {
    router.push(`/search?q=${encodeURIComponent(searchQuery.value)}`)
  }
}

function toggleNotifications() {
  showNotifications.value = !showNotifications.value
  showUserMenu.value = false
  
  // Mark all as read when opened
  if (showNotifications.value) {
    setTimeout(() => {
      notifications.value.forEach(n => n.read = true)
    }, 1000)
  }
}

function toggleUserMenu() {
  showUserMenu.value = !showUserMenu.value
  showNotifications.value = false
}

function clearNotifications() {
  notifications.value = []
  showNotifications.value = false
}

function toggleTheme() {
  isDarkMode.value = !isDarkMode.value
  document.documentElement.classList.toggle('light-mode')
}

function logout() {
  // Implement logout logic
  router.push('/login')
}

// Handle click outside to close dropdowns
function handleClickOutside(event) {
  const notificationWrapper = document.querySelector('.notification-wrapper')
  const userMenu = document.querySelector('.user-menu')
  
  if (notificationWrapper && !notificationWrapper.contains(event.target)) {
    showNotifications.value = false
  }
  
  if (userMenu && !userMenu.contains(event.target)) {
    showUserMenu.value = false
  }
}

// Keyboard shortcuts
function handleKeyboard(event) {
  // Cmd/Ctrl + K for search
  if ((event.metaKey || event.ctrlKey) && event.key === 'k') {
    event.preventDefault()
    document.querySelector('.search-input')?.focus()
  }
}

onMounted(() => {
  document.addEventListener('click', handleClickOutside)
  document.addEventListener('keydown', handleKeyboard)
})

onUnmounted(() => {
  document.removeEventListener('click', handleClickOutside)
  document.removeEventListener('keydown', handleKeyboard)
})
</script>

<style scoped>
.app-header {
  height: 72px;
  padding: 0 2rem;
  display: flex;
  align-items: center;
  justify-content: space-between;
  position: sticky;
  top: 0;
  z-index: 100;
  transition: all var(--transition-base);
}

.glass-header {
  background: var(--glass-bg);
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border-bottom: 1px solid var(--glass-border);
  box-shadow: 0 4px 24px rgba(0, 0, 0, 0.1);
}

/* Header Sections */
.header-left,
.header-center,
.header-right {
  display: flex;
  align-items: center;
}

.header-left {
  flex: 0 0 auto;
}

.header-center {
  flex: 1;
  max-width: 600px;
  margin: 0 2rem;
}

.header-right {
  flex: 0 0 auto;
  gap: 1.5rem;
}

/* Breadcrumb */
.breadcrumb {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.breadcrumb-item {
  color: var(--neutral-400);
  text-decoration: none;
  font-size: 0.875rem;
  font-weight: 500;
  transition: color var(--transition-base);
}

.breadcrumb-item:hover {
  color: var(--primary-400);
}

.breadcrumb-item.active {
  color: white;
}

.breadcrumb-separator {
  color: var(--neutral-600);
  font-size: 0.75rem;
}

/* Global Search */
.global-search {
  position: relative;
  width: 100%;
}

.search-icon {
  position: absolute;
  left: 1rem;
  top: 50%;
  transform: translateY(-50%);
  color: var(--neutral-500);
  pointer-events: none;
}

.search-input {
  width: 100%;
  padding: 0.75rem 3rem 0.75rem 2.75rem;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-lg);
  color: white;
  font-size: 0.875rem;
  transition: all var(--transition-base);
}

.search-input::placeholder {
  color: var(--neutral-500);
}

.search-input:focus {
  outline: none;
  background: rgba(255, 255, 255, 0.08);
  border-color: var(--primary-500);
  box-shadow: 0 0 0 3px rgba(168, 85, 247, 0.1);
}

.search-shortcut {
  position: absolute;
  right: 1rem;
  top: 50%;
  transform: translateY(-50%);
  padding: 0.25rem 0.5rem;
  background: var(--surface-300);
  border: 1px solid var(--surface-400);
  border-radius: var(--radius-sm);
  font-size: 0.75rem;
  color: var(--neutral-400);
  font-family: var(--font-mono);
}

/* Live Stats */
.live-stats {
  display: flex;
  gap: 0.75rem;
}

.stat-pill {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 0.75rem;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-full);
  transition: all var(--transition-base);
}

.stat-pill:hover {
  background: rgba(255, 255, 255, 0.08);
  transform: translateY(-1px);
}

.stat-icon {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.75rem;
}

.stat-icon.pending {
  background: rgba(59, 130, 246, 0.2);
  color: var(--info);
}

.stat-icon.processing {
  background: rgba(245, 158, 11, 0.2);
  color: var(--warning);
}

.stat-icon.completed {
  background: rgba(16, 185, 129, 0.2);
  color: var(--success);
}

.stat-value {
  font-size: 0.875rem;
  font-weight: 600;
  color: white;
}

/* Header Actions */
.header-actions {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.header-btn {
  width: 40px;
  height: 40px;
  border-radius: var(--radius-lg);
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--glass-border);
  color: var(--neutral-300);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all var(--transition-base);
  position: relative;
}

.header-btn:hover {
  background: rgba(168, 85, 247, 0.1);
  border-color: rgba(168, 85, 247, 0.3);
  color: var(--primary-400);
  transform: translateY(-2px);
}

/* Notifications */
.notification-wrapper {
  position: relative;
}

.notification-badge {
  position: absolute;
  top: -4px;
  right: -4px;
  min-width: 18px;
  height: 18px;
  padding: 0 5px;
  background: var(--gradient-primary);
  color: white;
  font-size: 0.625rem;
  font-weight: 700;
  border-radius: var(--radius-full);
  display: flex;
  align-items: center;
  justify-content: center;
}

.notifications-dropdown {
  position: absolute;
  top: calc(100% + 0.75rem);
  right: 0;
  width: 360px;
  max-height: 480px;
  padding: 0;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.dropdown-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1rem 1.25rem;
  border-bottom: 1px solid var(--glass-border);
}

.dropdown-header h4 {
  margin: 0;
  font-size: 1rem;
  color: white;
}

.clear-btn {
  background: none;
  border: none;
  color: var(--primary-400);
  font-size: 0.875rem;
  cursor: pointer;
  transition: color var(--transition-base);
}

.clear-btn:hover {
  color: var(--primary-300);
}

.notifications-list {
  flex: 1;
  overflow-y: auto;
  padding: 0.5rem;
}

.notification-item {
  display: flex;
  gap: 1rem;
  padding: 1rem;
  border-radius: var(--radius-lg);
  transition: all var(--transition-base);
  cursor: pointer;
}

.notification-item:hover {
  background: rgba(255, 255, 255, 0.05);
}

.notification-item.unread {
  background: rgba(168, 85, 247, 0.05);
  border-left: 3px solid var(--primary-500);
}

.notification-icon {
  width: 36px;
  height: 36px;
  border-radius: var(--radius-md);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.notification-icon.type-success {
  background: rgba(16, 185, 129, 0.1);
  color: var(--success);
}

.notification-icon.type-warning {
  background: rgba(245, 158, 11, 0.1);
  color: var(--warning);
}

.notification-icon.type-error {
  background: rgba(239, 68, 68, 0.1);
  color: var(--danger);
}

.notification-content {
  flex: 1;
  min-width: 0;
}

.notification-title {
  margin: 0 0 0.25rem;
  font-size: 0.875rem;
  font-weight: 600;
  color: white;
}

.notification-message {
  margin: 0 0 0.5rem;
  font-size: 0.75rem;
  color: var(--neutral-400);
  line-height: 1.4;
}

.notification-time {
  font-size: 0.625rem;
  color: var(--neutral-500);
}

.empty-notifications {
  padding: 3rem 1rem;
  text-align: center;
  color: var(--neutral-500);
}

.empty-notifications i {
  font-size: 2rem;
  margin-bottom: 1rem;
}

/* User Menu */
.user-menu {
  position: relative;
}

.user-avatar {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.5rem 0.75rem;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-lg);
  cursor: pointer;
  transition: all var(--transition-base);
}

.user-avatar:hover {
  background: rgba(255, 255, 255, 0.08);
  border-color: rgba(168, 85, 247, 0.3);
}

.user-avatar img {
  width: 32px;
  height: 32px;
  border-radius: 50%;
}

.user-name {
  font-size: 0.875rem;
  font-weight: 500;
  color: white;
}

.user-avatar i {
  font-size: 0.75rem;
  color: var(--neutral-400);
}

.user-dropdown {
  position: absolute;
  top: calc(100% + 0.75rem);
  right: 0;
  width: 200px;
  padding: 0.5rem;
}

.dropdown-item {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.75rem 1rem;
  color: var(--neutral-300);
  text-decoration: none;
  border-radius: var(--radius-md);
  transition: all var(--transition-base);
  cursor: pointer;
  background: none;
  border: none;
  width: 100%;
  text-align: left;
}

.dropdown-item:hover {
  background: rgba(168, 85, 247, 0.1);
  color: white;
}

.dropdown-item i {
  font-size: 0.875rem;
}

.dropdown-divider {
  height: 1px;
  background: var(--glass-border);
  margin: 0.5rem 0;
}

/* Animations */
.dropdown-enter-active,
.dropdown-leave-active {
  transition: all var(--transition-base);
}

.dropdown-enter-from,
.dropdown-leave-to {
  opacity: 0;
  transform: translateY(-10px);
}

/* Responsive */
@media (max-width: 1200px) {
  .header-center {
    display: none;
  }
  
  .live-stats {
    display: none;
  }
}

@media (max-width: 768px) {
  .app-header {
    padding: 0 1rem;
  }
  
  .user-name {
    display: none;
  }
  
  .notifications-dropdown {
    width: 320px;
    right: -1rem;
  }
}
</style>
