import type { LoadBalancer } from './http/LoadBalancer'

// ===== Client Configuration =====
export interface QueenConfig {
  urls?: string[]
  url?: string
  timeoutMillis?: number
  retryAttempts?: number
  retryDelayMillis?: number
  loadBalancingStrategy?: LoadBalancingStrategy
  affinityHashRing?: number
  enableFailover?: boolean
  healthRetryAfterMillis?: number
  bearerToken?: string | null
  headers?: Record<string, string>
}

export interface NormalizedQueenConfig {
  urls: string[]
  timeoutMillis: number
  retryAttempts: number
  retryDelayMillis: number
  loadBalancingStrategy: LoadBalancingStrategy
  affinityHashRing: number
  enableFailover: boolean
  healthRetryAfterMillis: number
  bearerToken: string | null
  headers: Record<string, string>
}

// ===== Queue Configuration =====
export interface QueueConfig {
  leaseTime?: number
  retryLimit?: number
  priority?: number
  delayedProcessing?: number
  windowBuffer?: number
  maxSize?: number
  retentionSeconds?: number
  completedRetentionSeconds?: number
  encryptionEnabled?: boolean
}

// ===== Messages =====
export interface QueenMessage {
  transactionId: string
  partitionId: string
  leaseId?: string
  queue?: string
  payload: unknown
  created?: string
  attempts?: number
  trace?: (config: TraceConfig) => Promise<TraceResult>
  _status?: boolean | string
  _error?: string
  id?: string
  [key: string]: unknown
}

export interface PushItem {
  data?: unknown
  payload?: unknown
  transactionId?: string
  traceId?: string
  [key: string]: unknown
}

export interface FormattedPushItem {
  queue: string
  partition: string
  payload: unknown
  transactionId: string
  traceId?: string
}

export interface PushResult {
  status: 'queued' | 'duplicate' | 'failed'
  error?: string
}

// ===== Consume Options =====
export interface ConsumeOptions {
  queue: string | null
  partition: string | null
  namespace: string | null
  task: string | null
  group: string | null
  concurrency: number
  batch: number
  limit: number | null
  idleMillis: number | null
  autoAck: boolean
  wait: boolean
  timeoutMillis: number
  renewLease: boolean
  renewLeaseIntervalMillis: number | null
  subscriptionMode: string | null
  subscriptionFrom: string | null
  each: boolean
  signal?: AbortSignal
}

// ===== Buffer Options =====
export interface BufferOptions {
  messageCount?: number
  timeMillis?: number
}

export interface ResolvedBufferOptions {
  messageCount: number
  timeMillis: number
}

// ===== Ack =====
export interface AckContext {
  error?: string
  group?: string
  consumerGroup?: string
}

export interface AckResult {
  success: boolean
  error?: string
  [key: string]: unknown
}

// ===== Renew =====
export interface RenewResult {
  leaseId: string
  success: boolean
  newExpiresAt?: string
  error?: string
}

// ===== Trace =====
export interface TraceConfig {
  traceName?: string | string[]
  eventType?: string
  data: Record<string, unknown>
}

export interface TraceResult {
  success: boolean
  error?: string
  [key: string]: unknown
}

// ===== DLQ =====
export interface DLQResult {
  messages: QueenMessage[]
  total: number
}

// ===== Buffer Stats =====
export interface BufferStats {
  activeBuffers: number
  totalBufferedMessages: number
  oldestBufferAge: number
  flushesPerformed: number
}

// ===== Transaction =====
export interface TransactionOperation {
  type: 'ack' | 'push'
  transactionId?: string
  partitionId?: string
  status?: string
  consumerGroup?: string
  items?: Array<{ queue: string; payload: unknown; partition?: string }>
}

export interface TransactionResult {
  success: boolean
  error?: string
}

// ===== HTTP =====
export interface HttpClientOptions {
  baseUrl?: string | null
  loadBalancer?: LoadBalancer | null
  timeoutMillis?: number
  retryAttempts?: number
  retryDelayMillis?: number
  enableFailover?: boolean
  bearerToken?: string | null
  headers?: Record<string, string>
}

// ===== Load Balancer =====
export type LoadBalancingStrategy = 'round-robin' | 'session' | 'affinity'

export interface LoadBalancerOptions {
  affinityHashRing?: number
  healthRetryAfterMillis?: number
}

export interface HealthStatus {
  healthy: boolean
  failures: number
  lastFailure: number | null
}

// ===== Stream =====
export interface StreamConfig {
  name: string
  namespace?: string
  source_queue_names: string[]
  partitioned: boolean
  window_type: string
  window_duration_ms: number
  window_grace_period_ms: number
  window_lease_timeout_ms: number
}

export interface AggregateConfig {
  count?: boolean
  sum?: string[]
  avg?: string[]
  min?: string[]
  max?: string[]
}

export interface AggregateResult {
  count?: number
  sum?: Record<string, number>
  avg?: Record<string, number>
  min?: Record<string, number | null>
  max?: Record<string, number | null>
}

export interface RawWindow {
  id?: string
  leaseId?: string
  messages?: unknown[]
  [key: string]: unknown
}

// ===== Worker Options =====
export interface WorkerOptions {
  batch: number
  limit: number | null
  idleMillis: number | null
  autoAck: boolean
  wait: boolean
  timeoutMillis: number
  renewLease: boolean
  renewLeaseIntervalMillis: number | null
  each: boolean
  signal?: AbortSignal
  group: string | null
  affinityKey: string | null
}
