import { ref, onUnmounted } from 'vue'

/**
 * Composable for API data fetching with loading, error, and auto-refresh
 */
export function useApi(fetchFn, options = {}) {
  const { 
    immediate = true, 
    refreshInterval = null,
    onSuccess = null,
    onError = null 
  } = options

  const data = ref(null)
  const loading = ref(false)
  const error = ref(null)
  const lastUpdated = ref(null)

  let intervalId = null

  const execute = async (...args) => {
    loading.value = true
    error.value = null

    try {
      const response = await fetchFn(...args)
      data.value = response.data
      lastUpdated.value = new Date()
      
      if (onSuccess) {
        onSuccess(response.data)
      }
      
      return response.data
    } catch (err) {
      error.value = err.message || 'An error occurred'
      
      if (onError) {
        onError(err)
      }
      
      throw err
    } finally {
      loading.value = false
    }
  }

  const refresh = () => execute()

  // Set up auto-refresh
  const startAutoRefresh = (interval = refreshInterval) => {
    if (interval && !intervalId) {
      intervalId = setInterval(refresh, interval)
    }
  }

  const stopAutoRefresh = () => {
    if (intervalId) {
      clearInterval(intervalId)
      intervalId = null
    }
  }

  // Initial fetch
  if (immediate) {
    execute()
  }

  // Start auto-refresh if configured
  if (refreshInterval) {
    startAutoRefresh()
  }

  // Cleanup on unmount
  onUnmounted(() => {
    stopAutoRefresh()
  })

  return {
    data,
    loading,
    error,
    lastUpdated,
    execute,
    refresh,
    startAutoRefresh,
    stopAutoRefresh
  }
}

/**
 * Format duration in milliseconds to human readable
 */
export function formatDuration(ms) {
  if (!ms || ms === 0) return '0ms'
  
  if (ms < 1000) return `${Math.round(ms)}ms`
  
  const seconds = ms / 1000
  if (seconds < 60) return `${seconds.toFixed(1)}s`
  
  const minutes = Math.floor(seconds / 60)
  const remainingSeconds = Math.round(seconds % 60)
  
  if (minutes < 60) {
    return remainingSeconds > 0 ? `${minutes}m ${remainingSeconds}s` : `${minutes}m`
  }
  
  const hours = Math.floor(minutes / 60)
  const remainingMinutes = minutes % 60
  
  if (hours < 24) {
    return remainingMinutes > 0 ? `${hours}h ${remainingMinutes}m` : `${hours}h`
  }
  
  const days = Math.floor(hours / 24)
  const remainingHours = hours % 24
  
  return remainingHours > 0 ? `${days}d ${remainingHours}h` : `${days}d`
}

/**
 * Format number with abbreviations for large values
 * - Numbers < 1M: show with commas (e.g., "999,999")
 * - Numbers >= 1M: show as "1.2M", "123.5M"
 * - Numbers >= 1B: show as "1.2B", "12.3B"
 */
export function formatNumber(num) {
  if (num === null || num === undefined) return '0'
  
  const absNum = Math.abs(num)
  const sign = num < 0 ? '-' : ''
  
  // Billions (1,000,000,000+)
  if (absNum >= 1_000_000_000) {
    const value = absNum / 1_000_000_000
    // Show 1 decimal for < 10B, no decimal for >= 10B
    const formatted = value >= 10 ? Math.round(value) : value.toFixed(1).replace(/\.0$/, '')
    return `${sign}${formatted}B`
  }
  
  // Millions (1,000,000+)
  if (absNum >= 1_000_000) {
    const value = absNum / 1_000_000
    // Show 1 decimal for < 10M, no decimal for >= 100M
    const formatted = value >= 100 
      ? Math.round(value)
      : value >= 10 
        ? value.toFixed(1).replace(/\.0$/, '')
        : value.toFixed(1).replace(/\.0$/, '')
    return `${sign}${formatted}M`
  }
  
  // Under 1 million: show with commas
  return num.toLocaleString('en-US')
}

/**
 * Format bytes to human readable
 */
export function formatBytes(bytes) {
  if (!bytes || bytes === 0) return '0 B'
  
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(2))} ${sizes[i]}`
}

/**
 * Format date/time
 */
export function formatDateTime(date) {
  if (!date) return '-'
  
  const d = new Date(date)
  return d.toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  })
}

/**
 * Format relative time
 */
export function formatRelativeTime(date) {
  if (!date) return '-'
  
  const now = new Date()
  const d = new Date(date)
  const diff = now - d
  
  if (diff < 60000) return 'just now'
  if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`
  if (diff < 86400000) return `${Math.floor(diff / 3600000)}h ago`
  return `${Math.floor(diff / 86400000)}d ago`
}

