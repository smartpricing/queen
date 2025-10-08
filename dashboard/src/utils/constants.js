// API Configuration
export const API_BASE_URL = import.meta.env.VITE_API_URL || 'http://localhost:6632'
export const API_VERSION = '/api/v1'
export const WS_URL = import.meta.env.VITE_WS_URL || 'ws://localhost:6632/ws/dashboard'

// Refresh intervals (in milliseconds)
export const REFRESH_INTERVALS = {
  DASHBOARD: 5000,      // 5 seconds
  ANALYTICS: 30000,     // 30 seconds
  QUEUE_DEPTH: 5000,    // 5 seconds
  SYSTEM_STATS: 10000   // 10 seconds
}

// Message statuses
export const MESSAGE_STATUS = {
  PENDING: 'pending',
  PROCESSING: 'processing',
  COMPLETED: 'completed',
  FAILED: 'failed',
  DEAD_LETTER: 'dead_letter'
}

// Status colors for UI
export const STATUS_COLORS = {
  [MESSAGE_STATUS.PENDING]: '#f59e0b',      // amber
  [MESSAGE_STATUS.PROCESSING]: '#3b82f6',   // blue
  [MESSAGE_STATUS.COMPLETED]: '#10b981',    // green
  [MESSAGE_STATUS.FAILED]: '#ef4444',       // red
  [MESSAGE_STATUS.DEAD_LETTER]: '#6b7280'   // gray
}

// Chart configuration
export const CHART_COLORS = [
  '#3b82f6', // blue
  '#10b981', // green
  '#f59e0b', // amber
  '#ef4444', // red
  '#8b5cf6', // purple
  '#ec4899', // pink
  '#14b8a6', // teal
  '#f97316', // orange
  '#06b6d4', // cyan
  '#84cc16'  // lime
]

// Pagination
export const DEFAULT_PAGE_SIZE = 20
export const PAGE_SIZE_OPTIONS = [10, 20, 50, 100]

// Date formats
export const DATE_FORMAT = {
  SHORT: 'MMM DD, HH:mm',
  LONG: 'MMM DD, YYYY HH:mm:ss',
  TIME_ONLY: 'HH:mm:ss',
  DATE_ONLY: 'MMM DD, YYYY'
}
