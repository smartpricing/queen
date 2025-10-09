/**
 * Centralized logging utility for Queen message queue
 * Logs all state-changing operations with consistent formatting
 */

/**
 * Log a message with timestamp and process ID
 * @param {...any} args - Arguments to log (objects will be JSON stringified)
 */
export function log(...args) {
  args = args.map(arg => typeof arg === 'object' ? JSON.stringify(arg) : arg);
  const msg = args.join(' ');
  console.log(new Date(), `| ${process.pid} | #> ${msg}`);
}

/**
 * Log operation types for consistency
 */
export const LogTypes = {
  // Message operations
  PUSH: 'PUSH',
  POP: 'POP',
  POP_FILTERED: 'POP_FILTERED',
  ACK: 'ACK',
  ACK_BATCH: 'ACK_BATCH',
  
  // Message state changes
  DELETE: 'DELETE',
  RETRY: 'RETRY',
  RETRY_SCHEDULED: 'RETRY_SCHEDULED',
  MOVED_TO_DLQ: 'MOVED_TO_DLQ',
  MANUAL_DLQ: 'MANUAL_DLQ',
  
  // Queue operations
  CLEAR_QUEUE: 'CLEAR_QUEUE',
  CONFIGURE_QUEUE: 'CONFIGURE_QUEUE',
  
  // System operations
  EVICTION: 'EVICTION',
  RETENTION: 'RETENTION',
  LEASE_EXPIRED: 'LEASE_EXPIRED'
};

export default log;
