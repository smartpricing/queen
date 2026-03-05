/**
 * Default configuration values for Queen Client
 * Following the convention:
 * - Properties with "Millis" suffix -> milliseconds
 * - Properties with "Seconds" suffix -> seconds
 * - Properties without suffix (time-related) -> seconds
 */

export const CLIENT_DEFAULTS = {
  timeoutMillis: 30000,
  retryAttempts: 3,
  retryDelayMillis: 1000,
  loadBalancingStrategy: 'affinity' as const,
  affinityHashRing: 128,
  enableFailover: true,
  healthRetryAfterMillis: 5000,
  bearerToken: null as string | null,
  headers: {} as Record<string, string>
}

export const QUEUE_DEFAULTS = {
  leaseTime: 300,
  retryLimit: 3,
  priority: 0,
  delayedProcessing: 0,
  windowBuffer: 0,
  maxSize: 0,
  retentionSeconds: 0,
  completedRetentionSeconds: 0,
  encryptionEnabled: false
}

export const CONSUME_DEFAULTS = {
  concurrency: 1,
  batch: 1,
  autoAck: true,
  wait: true,
  timeoutMillis: 30000,
  limit: null as number | null,
  idleMillis: null as number | null,
  renewLease: false,
  renewLeaseIntervalMillis: null as number | null,
  subscriptionMode: null as string | null,
  subscriptionFrom: null as string | null
}

export const POP_DEFAULTS = {
  batch: 1,
  wait: false,
  timeoutMillis: 30000,
  autoAck: false
}

export const BUFFER_DEFAULTS = {
  messageCount: 100,
  timeMillis: 1000
}
