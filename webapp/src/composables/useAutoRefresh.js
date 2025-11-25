import { ref, onMounted, onUnmounted, computed } from 'vue';

/**
 * Composable for auto-refreshing data at regular intervals
 * 
 * @param {Function} refreshCallback - The function to call for refreshing data
 * @param {Object} options - Configuration options
 * @param {number} options.interval - Refresh interval in milliseconds (default: 30000 = 30 seconds)
 * @param {boolean} options.immediate - Whether to call the refresh immediately on mount (default: true)
 * @param {boolean} options.enabled - Whether auto-refresh is enabled by default (default: true)
 * 
 * @returns {Object} - { isEnabled, isPaused, lastRefreshAt, toggle, pause, resume, refresh }
 */
export function useAutoRefresh(refreshCallback, options = {}) {
  const {
    interval = 30000, // 30 seconds by default
    immediate = true,
    enabled = true,
  } = options;

  const isEnabled = ref(enabled);
  const isPaused = ref(false);
  const lastRefreshAt = ref(null);
  const intervalId = ref(null);
  const isRefreshing = ref(false);

  /**
   * Perform the refresh
   */
  const refresh = async () => {
    if (isRefreshing.value) return; // Prevent concurrent refreshes
    
    try {
      isRefreshing.value = true;
      await refreshCallback();
      lastRefreshAt.value = new Date();
    } catch (error) {
      console.error('Auto-refresh error:', error);
      // Don't stop auto-refresh on error, just log it
    } finally {
      isRefreshing.value = false;
    }
  };

  /**
   * Start the auto-refresh interval
   */
  const startInterval = () => {
    if (intervalId.value) return; // Already running
    
    intervalId.value = setInterval(() => {
      if (isEnabled.value && !isPaused.value) {
        refresh();
      }
    }, interval);
  };

  /**
   * Stop the auto-refresh interval
   */
  const stopInterval = () => {
    if (intervalId.value) {
      clearInterval(intervalId.value);
      intervalId.value = null;
    }
  };

  /**
   * Toggle auto-refresh on/off
   */
  const toggle = () => {
    isEnabled.value = !isEnabled.value;
    
    if (isEnabled.value) {
      startInterval();
      // Refresh immediately when enabling
      refresh();
    } else {
      stopInterval();
    }
  };

  /**
   * Pause auto-refresh (temporary)
   */
  const pause = () => {
    isPaused.value = true;
  };

  /**
   * Resume auto-refresh
   */
  const resume = () => {
    isPaused.value = false;
  };

  /**
   * Time since last refresh (human-readable)
   */
  const timeSinceLastRefresh = computed(() => {
    if (!lastRefreshAt.value) return null;
    
    const seconds = Math.floor((new Date() - lastRefreshAt.value) / 1000);
    
    if (seconds < 60) return `${seconds}s ago`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
    return `${Math.floor(seconds / 3600)}h ago`;
  });

  // Set up on mount
  onMounted(() => {
    if (immediate && isEnabled.value) {
      refresh();
    }
    
    if (isEnabled.value) {
      startInterval();
    }
  });

  // Clean up on unmount
  onUnmounted(() => {
    stopInterval();
  });

  return {
    // State
    isEnabled,
    isPaused,
    lastRefreshAt,
    timeSinceLastRefresh,
    isRefreshing,
    
    // Methods
    toggle,
    pause,
    resume,
    refresh,
  };
}

