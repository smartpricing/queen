// Format numbers with abbreviations (1.2K, 3.4M, etc.)
export function formatNumber(num) {
  if (num === null || num === undefined) return '0'
  
  const absNum = Math.abs(num)
  if (absNum >= 1e9) return (num / 1e9).toFixed(1) + 'B'
  if (absNum >= 1e6) return (num / 1e6).toFixed(1) + 'M'
  if (absNum >= 1e3) return (num / 1e3).toFixed(1) + 'K'
  return num.toString()
}

// Format bytes to human readable
export function formatBytes(bytes, decimals = 2) {
  if (!bytes) return '0 Bytes'
  
  const k = 1024
  const dm = decimals < 0 ? 0 : decimals
  const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB']
  
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  
  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i]
}

// Format date to relative time (e.g., "2 hours ago")
export function formatRelativeTime(date) {
  if (!date) return ''
  
  const now = new Date()
  const then = new Date(date)
  const seconds = Math.floor((now - then) / 1000)
  
  if (seconds < 60) return 'just now'
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)} hours ago`
  if (seconds < 604800) return `${Math.floor(seconds / 86400)} days ago`
  
  return then.toLocaleDateString()
}

// Format date to local string
export function formatDate(date, format = 'long') {
  if (!date) return ''
  
  const d = new Date(date)
  
  switch (format) {
    case 'short':
      return d.toLocaleString('en-US', { 
        month: 'short', 
        day: 'numeric', 
        hour: '2-digit', 
        minute: '2-digit' 
      })
    case 'time':
      return d.toLocaleTimeString('en-US', { 
        hour: '2-digit', 
        minute: '2-digit', 
        second: '2-digit' 
      })
    case 'date':
      return d.toLocaleDateString('en-US', { 
        year: 'numeric', 
        month: 'short', 
        day: 'numeric' 
      })
    default:
      return d.toLocaleString('en-US', { 
        year: 'numeric', 
        month: 'short', 
        day: 'numeric', 
        hour: '2-digit', 
        minute: '2-digit', 
        second: '2-digit' 
      })
  }
}

// Calculate percentage
export function calculatePercentage(value, total) {
  if (!total || total === 0) return 0
  return Math.round((value / total) * 100)
}

// Get status severity for PrimeVue components
export function getStatusSeverity(status) {
  const severityMap = {
    'pending': 'warn',
    'processing': 'info',
    'completed': 'success',
    'failed': 'danger',
    'dead_letter': 'secondary'
  }
  return severityMap[status] || 'secondary'
}

// Debounce function for search inputs
export function debounce(func, wait) {
  let timeout
  return function executedFunction(...args) {
    const later = () => {
      clearTimeout(timeout)
      func(...args)
    }
    clearTimeout(timeout)
    timeout = setTimeout(later, wait)
  }
}

// Deep clone object
export function deepClone(obj) {
  return JSON.parse(JSON.stringify(obj))
}

// Generate unique ID
export function generateId() {
  return Date.now().toString(36) + Math.random().toString(36).substr(2)
}
