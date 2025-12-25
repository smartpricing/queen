<template>
  <header class="px-4 py-3 sm:px-6 sm:py-4 pl-14 sm:pl-16 lg:pl-6">
    <div class="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-3">
      <!-- Page title & breadcrumb -->
      <div class="min-w-0">
        <h1 class="text-xl sm:text-2xl font-display font-bold text-light-900 dark:text-white truncate">
          {{ pageTitle }}
        </h1>
        <p v-if="lastUpdated" class="text-xs sm:text-sm text-light-500 dark:text-light-500 mt-0.5">
          Last updated {{ lastUpdated }}
        </p>
      </div>
      
      <!-- Actions - scrollable on mobile -->
      <div class="flex items-center gap-2 sm:gap-3 overflow-x-auto pb-1 sm:pb-0 -mx-1 px-1 sm:mx-0 sm:px-0 scrollbar-hide">
        <!-- Search - expandable on mobile -->
        <div class="relative flex-shrink-0" ref="searchContainer">
          <!-- Mobile: Icon button to toggle search -->
          <button
            v-if="!showMobileSearch"
            @click="showMobileSearch = true"
            class="sm:hidden btn btn-ghost btn-icon"
          >
            <svg class="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
              <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
            </svg>
          </button>
          
          <!-- Search input - always visible on desktop, toggleable on mobile -->
          <div 
            v-show="showMobileSearch || !isMobile"
            class="relative"
          >
            <input
              ref="searchInput"
              v-model="searchQuery"
              type="text"
              placeholder="Search..."
              class="w-40 sm:w-48 lg:w-64 pl-9 pr-8 py-2 rounded-lg text-sm
                     bg-white dark:bg-dark-200 
                     border border-light-300/50 dark:border-dark-50/50
                     text-light-800 dark:text-light-200
                     placeholder:text-light-500
                     focus:outline-none focus:border-queen-500 focus:ring-2 focus:ring-queen-500/20
                     transition-all duration-200"
              @focus="onSearchFocus"
              @input="onSearchInput"
              @blur="onSearchBlur"
              @keydown.enter="handleSearchEnter"
              @keydown.down.prevent="navigateResults(1)"
              @keydown.up.prevent="navigateResults(-1)"
              @keydown.escape="closeSearch"
            />
            <svg 
              class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-light-500"
              fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
            >
              <path stroke-linecap="round" stroke-linejoin="round" d="M21 21l-5.197-5.197m0 0A7.5 7.5 0 105.196 5.196a7.5 7.5 0 0010.607 10.607z" />
            </svg>
            
            <!-- Close button on mobile -->
            <button
              v-if="showMobileSearch && isMobile"
              @click="closeSearch"
              class="absolute right-2 top-1/2 -translate-y-1/2 p-1 rounded hover:bg-light-100 dark:hover:bg-dark-100"
            >
              <svg class="w-4 h-4 text-light-400" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
                <path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
              </svg>
            </button>
            
            <!-- Loading indicator -->
            <div 
              v-if="searchLoading && !isMobile" 
              class="absolute right-3 top-1/2 -translate-y-1/2"
            >
              <svg class="w-4 h-4 animate-spin text-light-400" fill="none" viewBox="0 0 24 24">
                <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
                <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
              </svg>
            </div>
          </div>
          
          <!-- Search Results Dropdown -->
          <div 
            v-if="showResults && searchQuery.length > 0 && searchResults.length > 0"
            class="absolute top-full left-0 right-0 sm:left-auto sm:right-0 sm:w-72 mt-1 bg-white dark:bg-dark-200 rounded-lg shadow-lg border border-light-300/50 dark:border-dark-50/50 overflow-hidden z-50 max-h-80 overflow-y-auto"
          >
            <div 
              v-for="(result, index) in searchResults" 
              :key="result.id"
              @mousedown.prevent="selectResult(result)"
              class="px-4 py-2.5 cursor-pointer flex items-center gap-3 transition-colors"
              :class="[
                index === selectedIndex 
                  ? 'bg-queen-50 dark:bg-queen-900/20' 
                  : 'hover:bg-light-100 dark:hover:bg-dark-100'
              ]"
            >
              <span 
                class="flex-shrink-0 w-6 h-6 rounded flex items-center justify-center text-xs font-medium"
                :class="result.type === 'queue' 
                  ? 'bg-cyber-100 dark:bg-cyber-900/30 text-cyber-700 dark:text-cyber-400'
                  : 'bg-crown-100 dark:bg-crown-900/30 text-crown-700 dark:text-crown-400'"
              >
                {{ result.type === 'queue' ? 'Q' : 'CG' }}
              </span>
              <div class="flex-1 min-w-0">
                <p class="text-sm font-medium text-light-900 dark:text-white truncate">
                  {{ result.name }}
                </p>
                <p class="text-xs text-light-500 truncate">
                  {{ result.type === 'queue' 
                    ? `${result.partitions} partitions` 
                    : `${result.queueName} · ${result.members} members` }}
                </p>
              </div>
            </div>
          </div>
          
          <!-- No results -->
          <div 
            v-if="showResults && searchQuery.length > 0 && searchResults.length === 0 && !searchLoading"
            class="absolute top-full left-0 right-0 sm:left-auto sm:right-0 sm:w-72 mt-1 bg-white dark:bg-dark-200 rounded-lg shadow-lg border border-light-300/50 dark:border-dark-50/50 overflow-hidden z-50"
          >
            <div class="px-4 py-3 text-sm text-light-500 text-center">
              No results found
            </div>
          </div>
        </div>
        
        <!-- Refresh button -->
        <button 
          @click="handleRefresh"
          :disabled="isRefreshing"
          class="btn btn-ghost btn-icon flex-shrink-0"
          title="Refresh"
        >
          <svg 
            class="w-5 h-5 transition-transform duration-300" 
            :class="{ 'animate-spin': isRefreshing }"
            fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5"
          >
            <path stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0l3.181 3.183a8.25 8.25 0 0013.803-3.7M4.031 9.865a8.25 8.25 0 0113.803-3.7l3.181 3.182m0-4.991v4.99" />
          </svg>
        </button>
        
        <!-- Maintenance Mode Controls - Compact on mobile -->
        <div class="flex items-center rounded-lg border border-light-300/50 dark:border-dark-50/50 overflow-hidden flex-shrink-0">
          <!-- Push Maintenance -->
          <button
            @click="togglePushMaintenance"
            :disabled="pushLoading"
            class="flex items-center gap-1 px-2 sm:px-3 py-1.5 text-xs font-medium transition-colors"
            :class="[
              pushMaintenanceMode 
                ? 'bg-yellow-100 dark:bg-yellow-900/30 text-yellow-700 dark:text-yellow-400' 
                : 'bg-white dark:bg-dark-200 text-light-600 dark:text-light-400 hover:bg-light-100 dark:hover:bg-dark-100'
            ]"
            :title="pushMaintenanceMode 
              ? `Push Maintenance ON (${formatNumber(bufferedMessages)} buffered)` 
              : 'Push Maintenance OFF'"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.5">
              <path stroke-linecap="round" stroke-linejoin="round" d="M11.42 15.17L17.25 21A2.652 2.652 0 0021 17.25l-5.877-5.877M11.42 15.17l2.496-3.03c.317-.384.74-.626 1.208-.766M11.42 15.17l-4.655 5.653a2.548 2.548 0 11-3.586-3.586l6.837-5.63m5.108-.233c.55-.164 1.163-.188 1.743-.14a4.5 4.5 0 004.486-6.336l-3.276 3.277a3.004 3.004 0 01-2.25-2.25l3.276-3.276a4.5 4.5 0 00-6.336 4.486c.091 1.076-.071 2.264-.904 2.95l-.102.085m-1.745 1.437L5.909 7.5H4.5L2.25 3.75l1.5-1.5L7.5 4.5v1.409l4.26 4.26m-1.745 1.437l1.745-1.437m6.615 8.206L15.75 15.75M4.867 19.125h.008v.008h-.008v-.008z" />
            </svg>
            <span class="hidden sm:inline">Push</span>
            <span 
              v-if="pushMaintenanceMode" 
              class="w-1.5 h-1.5 rounded-full bg-yellow-500"
            />
            <span 
              v-if="pushMaintenanceMode && bufferedMessages > 0" 
              class="hidden sm:inline text-[10px] bg-yellow-500/20 px-1 rounded"
            >
              {{ formatNumber(bufferedMessages) }}
            </span>
          </button>
          
          <!-- Divider -->
          <div class="w-px h-6 bg-light-300/50 dark:bg-dark-50/50" />
          
          <!-- Pop Maintenance -->
          <button
            @click="togglePopMaintenance"
            :disabled="popLoading"
            class="flex items-center gap-1 px-2 sm:px-3 py-1.5 text-xs font-medium transition-colors"
            :class="[
              popMaintenanceMode 
                ? 'bg-orange-100 dark:bg-orange-900/30 text-orange-700 dark:text-orange-400' 
                : 'bg-white dark:bg-dark-200 text-light-600 dark:text-light-400 hover:bg-light-100 dark:hover:bg-dark-100'
            ]"
            :title="popMaintenanceMode 
              ? 'Pop Maintenance ON - Consumers paused' 
              : 'Pop Maintenance OFF'"
          >
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24" stroke-width="1.5">
              <path stroke-linecap="round" stroke-linejoin="round" d="M15.75 17.25v3.375c0 .621-.504 1.125-1.125 1.125h-9.75a1.125 1.125 0 01-1.125-1.125V7.875c0-.621.504-1.125 1.125-1.125H6.75a9.06 9.06 0 011.5.124m7.5 10.376h3.375c.621 0 1.125-.504 1.125-1.125V11.25c0-4.46-3.243-8.161-7.5-8.876a9.06 9.06 0 00-1.5-.124H9.375c-.621 0-1.125.504-1.125 1.125v3.5m7.5 10.375H9.375a1.125 1.125 0 01-1.125-1.125v-9.25m12 6.625v-1.875a3.375 3.375 0 00-3.375-3.375h-1.5a1.125 1.125 0 01-1.125-1.125v-1.5a3.375 3.375 0 00-3.375-3.375H9.75" />
            </svg>
            <span class="hidden sm:inline">Pop</span>
            <span 
              v-if="popMaintenanceMode" 
              class="w-1.5 h-1.5 rounded-full bg-orange-500"
            />
          </button>
        </div>
        
        <!-- Theme toggle -->
        <button 
          @click="toggleTheme"
          class="btn btn-ghost btn-icon flex-shrink-0"
          title="Toggle theme"
        >
          <svg v-if="isDark" class="w-5 h-5 text-crown-400" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
            <path stroke-linecap="round" stroke-linejoin="round" d="M12 3v2.25m6.364.386l-1.591 1.591M21 12h-2.25m-.386 6.364l-1.591-1.591M12 18.75V21m-4.773-4.227l-1.591 1.591M5.25 12H3m4.227-4.773L5.636 5.636M15.75 12a3.75 3.75 0 11-7.5 0 3.75 3.75 0 017.5 0z" />
          </svg>
          <svg v-else class="w-5 h-5 text-light-600" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="1.5">
            <path stroke-linecap="round" stroke-linejoin="round" d="M21.752 15.002A9.718 9.718 0 0118 15.75c-5.385 0-9.75-4.365-9.75-9.75 0-1.33.266-2.597.748-3.752A9.753 9.753 0 003 11.25C3 16.635 7.365 21 12.75 21a9.753 9.753 0 009.002-5.998z" />
          </svg>
        </button>
      </div>
    </div>
  </header>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useTheme } from '@/composables/useTheme'
import { formatRelativeTime } from '@/composables/useApi'
import { queues as queuesApi, consumers as consumersApi, system } from '@/api'

const route = useRoute()
const router = useRouter()
const { isDark, toggleTheme } = useTheme()

const emit = defineEmits(['refresh'])

// Mobile detection
const isMobile = ref(false)
const showMobileSearch = ref(false)
const searchInput = ref(null)

const checkMobile = () => {
  isMobile.value = window.innerWidth < 640
}

// Search state
const searchQuery = ref('')
const showResults = ref(false)
const selectedIndex = ref(0)
const searchContainer = ref(null)
const searchLoading = ref(false)
const searchDataLoaded = ref(false)

// Data for search
const queues = ref([])
const consumers = ref([])

// Refresh state
const isRefreshing = ref(false)

// Maintenance state
const pushMaintenanceMode = ref(false)
const popMaintenanceMode = ref(false)
const bufferedMessages = ref(0)
const pushLoading = ref(false)
const popLoading = ref(false)

const pageTitle = computed(() => {
  return route.meta.title || 'Dashboard'
})

const lastUpdated = computed(() => {
  return formatRelativeTime(new Date())
})

// Search results
const searchResults = computed(() => {
  if (!searchQuery.value) return []
  
  const query = searchQuery.value.toLowerCase()
  const results = []
  
  // Search queues
  queues.value
    .filter(q => q.name && q.name.toLowerCase().includes(query))
    .slice(0, 5)
    .forEach(q => {
      results.push({
        id: `queue-${q.name}`,
        type: 'queue',
        name: q.name,
        partitions: q.partitions || 1,
        route: `/queues/${encodeURIComponent(q.name)}`
      })
    })
  
  // Search consumer groups - navigate to consumers page with search filter
  consumers.value
    .filter(c => c.name && c.name.toLowerCase().includes(query))
    .slice(0, 5)
    .forEach(c => {
      results.push({
        id: `consumer-${c.name}-${c.queueName}`,
        type: 'consumer',
        name: c.name,
        queueName: c.queueName,
        members: c.members || 0,
        route: `/consumers?search=${encodeURIComponent(c.name)}`
      })
    })
  
  return results.slice(0, 10)
})

// Handle search keyboard navigation
const navigateResults = (direction) => {
  if (searchResults.value.length === 0) return
  
  selectedIndex.value = Math.max(
    0, 
    Math.min(
      searchResults.value.length - 1, 
      selectedIndex.value + direction
    )
  )
}

const handleSearchEnter = () => {
  if (searchResults.value.length > 0 && selectedIndex.value < searchResults.value.length) {
    selectResult(searchResults.value[selectedIndex.value])
  }
}

const selectResult = (result) => {
  router.push(result.route)
  searchQuery.value = ''
  showResults.value = false
  showMobileSearch.value = false
  selectedIndex.value = 0
}

const closeSearch = () => {
  showResults.value = false
  showMobileSearch.value = false
  searchQuery.value = ''
}

const onSearchBlur = () => {
  // Delay to allow click on results
  setTimeout(() => {
    if (isMobile.value && !searchQuery.value) {
      showMobileSearch.value = false
    }
  }, 200)
}

// Close search on outside click
const handleClickOutside = (event) => {
  if (searchContainer.value && !searchContainer.value.contains(event.target)) {
    showResults.value = false
  }
}

// Reset selected index when search query changes
watch(searchQuery, () => {
  selectedIndex.value = 0
  showResults.value = searchQuery.value.length > 0
})

// Focus search input when mobile search is shown
watch(showMobileSearch, async (show) => {
  if (show && searchInput.value) {
    await nextTick()
    searchInput.value.focus()
  }
})

// Search focus handler - load data if not already loaded
const onSearchFocus = async () => {
  showResults.value = searchQuery.value.length > 0
  if (!searchDataLoaded.value) {
    await loadSearchData()
  }
}

// Search input handler
const onSearchInput = () => {
  showResults.value = true
}

// Refresh handler
const handleRefresh = async () => {
  isRefreshing.value = true
  emit('refresh')
  
  // Also refresh search data
  await loadSearchData()
  await loadMaintenanceStatus()
  
  setTimeout(() => {
    isRefreshing.value = false
  }, 500)
}

// Load data for search
const loadSearchData = async () => {
  if (searchLoading.value) return
  
  searchLoading.value = true
  try {
    const [queuesRes, consumersRes] = await Promise.all([
      queuesApi.list(),
      consumersApi.list()
    ])
    // Queues API returns { queues: [...] }, consumers returns [...]
    queues.value = queuesRes.data?.queues || queuesRes.data || []
    consumers.value = Array.isArray(consumersRes.data) ? consumersRes.data : []
    searchDataLoaded.value = true
  } catch (error) {
    console.error('Failed to load search data:', error)
  } finally {
    searchLoading.value = false
  }
}

// Maintenance functions
const loadMaintenanceStatus = async () => {
  try {
    const response = await system.getMaintenance()
    pushMaintenanceMode.value = response.data.maintenanceMode || false
    popMaintenanceMode.value = response.data.popMaintenanceMode || false
    bufferedMessages.value = response.data.bufferedMessages || 0
  } catch (error) {
    console.error('Failed to load maintenance status:', error)
  }
}

const togglePushMaintenance = async () => {
  if (pushLoading.value) return
  
  const enable = !pushMaintenanceMode.value
  
  // Confirm action
  if (enable) {
    if (!confirm('Enable PUSH maintenance mode?\n\nAll PUSH operations will be routed to file buffer until maintenance is disabled.')) {
      return
    }
  } else {
    if (bufferedMessages.value > 0) {
      if (!confirm(`Disable PUSH maintenance mode?\n\n${formatNumber(bufferedMessages.value)} buffered messages will be drained to the database.`)) {
        return
      }
    }
  }
  
  pushLoading.value = true
  
  try {
    const response = await system.setMaintenance(enable)
    pushMaintenanceMode.value = response.data.maintenanceMode || false
    bufferedMessages.value = response.data.bufferedMessages || 0
    
    if (enable) {
      alert('✅ PUSH maintenance mode enabled.\n\nAll PUSH operations are now routing to file buffer.')
    } else {
      alert(`✅ PUSH maintenance mode disabled.\n\n${bufferedMessages.value > 0 ? 'File buffer is draining to database.' : 'No buffered messages.'}`)
    }
  } catch (error) {
    alert(`❌ Failed to ${enable ? 'enable' : 'disable'} PUSH maintenance mode:\n\n${error.message}`)
  } finally {
    pushLoading.value = false
  }
}

const togglePopMaintenance = async () => {
  if (popLoading.value) return
  
  const enable = !popMaintenanceMode.value
  
  // Confirm action
  if (enable) {
    if (!confirm('Enable POP maintenance mode?\n\nAll POP operations will return empty arrays.\nConsumers will receive no messages until disabled.')) {
      return
    }
  } else {
    if (!confirm('Disable POP maintenance mode?\n\nConsumers will start receiving messages again.')) {
      return
    }
  }
  
  popLoading.value = true
  
  try {
    const response = await system.setPopMaintenance(enable)
    popMaintenanceMode.value = response.data.popMaintenanceMode || false
    
    if (enable) {
      alert('✅ POP maintenance mode enabled.\n\nAll consumers are now receiving empty responses.')
    } else {
      alert('✅ POP maintenance mode disabled.\n\nConsumers will resume receiving messages.')
    }
  } catch (error) {
    alert(`❌ Failed to ${enable ? 'enable' : 'disable'} POP maintenance mode:\n\n${error.message}`)
  } finally {
    popLoading.value = false
  }
}

function formatNumber(num) {
  if (num >= 1000000) return `${(num / 1000000).toFixed(1)}M`
  if (num >= 1000) return `${(num / 1000).toFixed(1)}K`
  return num.toString()
}

// Initialize
let maintenanceInterval = null

onMounted(() => {
  checkMobile()
  window.addEventListener('resize', checkMobile)
  
  loadSearchData()
  loadMaintenanceStatus()
  
  // Refresh maintenance status every 30 seconds
  maintenanceInterval = setInterval(loadMaintenanceStatus, 30000)
  
  // Add click outside listener
  document.addEventListener('click', handleClickOutside)
})

onUnmounted(() => {
  window.removeEventListener('resize', checkMobile)
  
  if (maintenanceInterval) {
    clearInterval(maintenanceInterval)
  }
  document.removeEventListener('click', handleClickOutside)
})
</script>
