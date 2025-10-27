/**
 * Default configuration values for Queen Client
 * Following the convention:
 * - Properties with "Millis" suffix → milliseconds
 * - Properties with "Seconds" suffix → seconds  
 * - Properties without suffix (time-related) → seconds
 */

export const CLIENT_DEFAULTS = {
  timeoutMillis: 30000,                // 30 seconds
  retryAttempts: 3,                    // 3 retry attempts
  retryDelayMillis: 1000,              // 1 second initial delay (exponential backoff)
  loadBalancingStrategy: 'round-robin', // 'round-robin' or 'session'
  enableFailover: true                 // Auto-failover to other servers
}

export const QUEUE_DEFAULTS = {
  leaseTime: 300,                      // 5 minutes (seconds)
  retryLimit: 3,                       // Max 3 retries before DLQ
  priority: 0,                         // Default priority
  delayedProcessing: 0,                // No delay (seconds)
  windowBuffer: 0,                     // No window buffering (seconds)
  maxSize: 10000,                      // Max 10,000 messages per queue
  retentionSeconds: 0,                 // No retention (keep forever)
  completedRetentionSeconds: 0,        // No retention for completed messages
  encryptionEnabled: false             // No encryption by default
}

export const CONSUME_DEFAULTS = {
  concurrency: 1,                      // Single worker
  batch: 1,                            // One message at a time
  autoAck: true,                       // Auto-acknowledge by default
  wait: true,                          // Long polling enabled
  timeoutMillis: 30000,                // 30 seconds long poll timeout
  limit: null,                         // No limit (run forever)
  idleMillis: null,                    // No idle timeout
  renewLease: false,                   // No auto-renewal
  renewLeaseIntervalMillis: null,      // Auto-renewal interval when enabled
  subscriptionMode: null,              // No subscription mode (standard queue mode)
  subscriptionFrom: null               // No subscription start point
}

export const POP_DEFAULTS = {
  batch: 1,                            // One message
  wait: false,                         // No long polling (immediate return)
  timeoutMillis: 30000,                // 30 seconds if wait=true
  autoAck: false                       // Manual ack required
}

export const BUFFER_DEFAULTS = {
  messageCount: 100,                   // Flush after 100 messages
  timeMillis: 1000                     // Or flush after 1 second
}

