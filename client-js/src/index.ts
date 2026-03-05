/**
 * Queen Message Queue Client - Entry Point
 */

export { Queen } from './Queen'
export { Admin } from './admin/Admin'
export { CLIENT_DEFAULTS, QUEUE_DEFAULTS, CONSUME_DEFAULTS, POP_DEFAULTS, BUFFER_DEFAULTS } from './utils/defaults'

// Re-export types
export type {
  QueenConfig,
  NormalizedQueenConfig,
  QueueConfig,
  QueenMessage,
  PushItem,
  FormattedPushItem,
  PushResult,
  ConsumeOptions,
  BufferOptions,
  ResolvedBufferOptions,
  AckContext,
  AckResult,
  RenewResult,
  TraceConfig,
  TraceResult,
  DLQResult,
  BufferStats,
  TransactionOperation,
  TransactionResult,
  HttpClientOptions,
  LoadBalancingStrategy,
  LoadBalancerOptions,
  HealthStatus,
  StreamConfig,
  AggregateConfig,
  AggregateResult,
  RawWindow,
  WorkerOptions
} from './types'

// Re-export classes that may be useful
export { QueueBuilder, OperationBuilder, ConsumeBuilder, PushBuilder, DLQBuilder } from './builders/QueueBuilder'
export { TransactionBuilder } from './builders/TransactionBuilder'
export { HttpClient } from './http/HttpClient'
export { LoadBalancer } from './http/LoadBalancer'
export { BufferManager } from './buffer/BufferManager'
export { MessageBuffer } from './buffer/MessageBuffer'
export { StreamBuilder } from './stream/StreamBuilder'
export { StreamConsumer } from './stream/StreamConsumer'
export { Window } from './stream/Window'
export { ConsumerManager } from './consumer/ConsumerManager'
export { generateUUID } from './builders/QueueBuilder'
