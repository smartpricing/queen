/**
 * Logger utility for Queen Client v2
 * Controlled by QUEEN_CLIENT_LOG environment variable (Node.js)
 * or window.QUEEN_CLIENT_LOG (Browser)
 */

declare const window: { QUEEN_CLIENT_LOG?: boolean } | undefined

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

function getTimestamp(): string {
  return new Date().toISOString()
}

function formatLog(operation: string, details: unknown, level: string = 'INFO'): string {
  const timestamp = getTimestamp()
  const detailsStr = typeof details === 'object' ? JSON.stringify(details) : details
  return `[${timestamp}] [${level}] [${operation}] ${detailsStr}`
}

export function log(operation: string, details: unknown): void {
  if (!LOG_ENABLED) return
  console.log(formatLog(operation, details))
}

export function warn(operation: string, details: unknown): void {
  if (!LOG_ENABLED) return
  console.warn(formatLog(operation, details, 'WARN'))
}

export function error(operation: string, details: unknown): void {
  if (!LOG_ENABLED) return
  console.error(formatLog(operation, details, 'ERROR'))
}

export function isEnabled(): boolean {
  return LOG_ENABLED
}
