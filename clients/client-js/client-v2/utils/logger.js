/**
 * Logger utility for Queen Client v2
 * 
 * Supports pluggable logger backends (pino, winston, bunyan, etc.).
 * When no custom logger is configured, falls back to console-based logging
 * gated by the QUEEN_CLIENT_LOG environment variable (Node.js)
 * or window.QUEEN_CLIENT_LOG (Browser).
 */

const LOG_ENABLED = (() => {
  // Node.js environment
  if (typeof process !== 'undefined' && process.env) {
    return process.env.QUEEN_CLIENT_LOG === 'true'
  }
  // Browser environment
  if (typeof window !== 'undefined') {
    return window.QUEEN_CLIENT_LOG === true
  }
  return false
})()

let customLogger = null

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
 * Configure a custom logger backend.
 * The logger must implement: info(msg), warn(msg), error(msg).
 * debug(msg) is optional and falls back to info(msg) if missing.
 * When a custom logger is set, it is always active (no env var gating).
 * @param {object} logger - Logger instance (e.g. pino(), winston.createLogger())
 */
export function configure(logger) {
  if (logger && typeof logger.info !== 'function') {
    throw new Error('Custom logger must implement info(), warn(), and error() methods')
  }
  customLogger = logger
}

export function log(operation, details) {
  if (customLogger) {
    const detailsStr = typeof details === 'object' ? JSON.stringify(details) : details
    customLogger.info(`[${operation}] ${detailsStr}`)
    return
  }
  if (!LOG_ENABLED) return
  console.log(formatLog(operation, details))
}

export function debug(operation, details) {
  if (customLogger) {
    const detailsStr = typeof details === 'object' ? JSON.stringify(details) : details
    const fn = customLogger.debug || customLogger.info
    fn.call(customLogger, `[${operation}] ${detailsStr}`)
    return
  }
  if (!LOG_ENABLED) return
  console.log(formatLog(operation, details, 'DEBUG'))
}

export function warn(operation, details) {
  if (customLogger) {
    const detailsStr = typeof details === 'object' ? JSON.stringify(details) : details
    customLogger.warn(`[${operation}] ${detailsStr}`)
    return
  }
  if (!LOG_ENABLED) return
  console.warn(formatLog(operation, details, 'WARN'))
}

export function error(operation, details) {
  if (customLogger) {
    const detailsStr = typeof details === 'object' ? JSON.stringify(details) : details
    customLogger.error(`[${operation}] ${detailsStr}`)
    return
  }
  if (!LOG_ENABLED) return
  console.error(formatLog(operation, details, 'ERROR'))
}

/**
 * Check if logging is enabled
 */
export function isEnabled() {
  return customLogger != null || LOG_ENABLED
}
