/**
 * Logger utility for Queen Client v2
 * Controlled by QUEEN_CLIENT_LOG environment variable
 */

const LOG_ENABLED = process.env.QUEEN_CLIENT_LOG === 'true'

/**
 * Get formatted timestamp
 */
function getTimestamp() {
  return new Date().toISOString()
}

/**
 * Format log message with timestamp and operation
 */
function formatLog(operation, details, level = 'INFO') {
  const timestamp = getTimestamp()
  const detailsStr = typeof details === 'object' ? JSON.stringify(details) : details
  return `[${timestamp}] [${level}] [${operation}] ${detailsStr}`
}

/**
 * Log an operation
 */
export function log(operation, details) {
  if (!LOG_ENABLED) return
  console.log(formatLog(operation, details))
}

/**
 * Log a warning
 */
export function warn(operation, details) {
  if (!LOG_ENABLED) return
  console.warn(formatLog(operation, details, 'WARN'))
}

/**
 * Log an error
 */
export function error(operation, details) {
  if (!LOG_ENABLED) return
  console.error(formatLog(operation, details, 'ERROR'))
}

/**
 * Check if logging is enabled
 */
export function isEnabled() {
  return LOG_ENABLED
}

