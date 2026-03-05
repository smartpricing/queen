/**
 * Queen Message Queue Client - Version 2
 * Clean, fluent API with smart defaults
 */

import { HttpClient } from './http/HttpClient'
import { LoadBalancer } from './http/LoadBalancer'
import { BufferManager } from './buffer/BufferManager'
import { QueueBuilder } from './builders/QueueBuilder'
import { TransactionBuilder } from './builders/TransactionBuilder'
import { StreamBuilder } from './stream/StreamBuilder'
import { StreamConsumer } from './stream/StreamConsumer'
import { Admin } from './admin/Admin'
import { CLIENT_DEFAULTS } from './utils/defaults'
import { validateUrl, validateUrls } from './utils/validation'
import * as logger from './utils/logger'
import type { QueenConfig, NormalizedQueenConfig, QueenMessage, AckContext, AckResult, RenewResult, BufferStats } from './types'

export class Queen {
  private httpClient: HttpClient
  private bufferManager: BufferManager
  private _config: NormalizedQueenConfig
  private shutdownHandlers: (() => void)[] = []
  private _admin: Admin | null = null

  constructor(config: QueenConfig | string | string[] = {}) {
    logger.log('Queen.constructor', { config: typeof config === 'object' && !Array.isArray(config) ? { ...config, urls: config.urls?.length || 0 } : { type: typeof config } })

    this._config = this.normalizeConfig(config)

    this.httpClient = this.createHttpClient()

    this.bufferManager = new BufferManager(this.httpClient)

    this.setupGracefulShutdown()

    logger.log('Queen.constructor', { status: 'initialized', urls: this._config.urls.length })
  }

  private normalizeConfig(config: QueenConfig | string | string[]): NormalizedQueenConfig {
    if (typeof config === 'string') {
      return {
        ...CLIENT_DEFAULTS,
        urls: [validateUrl(config)]
      }
    }

    if (Array.isArray(config)) {
      return {
        ...CLIENT_DEFAULTS,
        urls: validateUrls(config)
      }
    }

    const normalized = {
      ...CLIENT_DEFAULTS,
      ...config
    } as NormalizedQueenConfig & { url?: string }

    if (normalized.urls) {
      normalized.urls = validateUrls(normalized.urls)
    } else if (normalized.url) {
      normalized.urls = [validateUrl(normalized.url)]
    } else {
      throw new Error('Must provide urls or url in configuration')
    }

    delete normalized.url

    return normalized as NormalizedQueenConfig
  }

  private createHttpClient(): HttpClient {
    const { urls, timeoutMillis, retryAttempts, retryDelayMillis, loadBalancingStrategy, affinityHashRing, healthRetryAfterMillis, enableFailover, bearerToken, headers } = this._config

    if (urls.length === 1) {
      return new HttpClient({
        baseUrl: urls[0],
        timeoutMillis,
        retryAttempts,
        retryDelayMillis,
        bearerToken,
        headers
      })
    }

    const loadBalancer = new LoadBalancer(urls, loadBalancingStrategy, {
      affinityHashRing,
      healthRetryAfterMillis
    })
    return new HttpClient({
      loadBalancer,
      timeoutMillis,
      retryAttempts,
      retryDelayMillis,
      enableFailover,
      bearerToken,
      headers
    })
  }

  private setupGracefulShutdown(): void {
    if (typeof process === 'undefined' || typeof process.on !== 'function') {
      return
    }

    let signalReceivedCount = 0
    const shutdown = async (signal: string) => {
      console.log(`\nReceived ${signal}, shutting down gracefully...`)
      try {
        signalReceivedCount++
        if (signalReceivedCount > 1) {
          console.log('Received multiple shutdown signals, exiting immediately')
          process.exit(1)
        }
        await this.close()
        process.exit(0)
      } catch (error) {
        console.error('Error during shutdown:', error)
        process.exit(1)
      }
    }

    const sigintHandler = () => shutdown('SIGINT')
    const sigtermHandler = () => shutdown('SIGTERM')

    process.on('SIGINT', sigintHandler)
    process.on('SIGTERM', sigtermHandler)

    this.shutdownHandlers.push(
      () => process.off('SIGINT', sigintHandler),
      () => process.off('SIGTERM', sigtermHandler)
    )
  }

  // ===========================
  // Queue Builder Entry Point
  // ===========================

  queue(name: string | null = null): QueueBuilder {
    return new QueueBuilder(this, this.httpClient, this.bufferManager, name)
  }

  // ===========================
  // Admin API Entry Point
  // ===========================

  get admin(): Admin {
    if (!this._admin) {
      this._admin = new Admin(this.httpClient)
    }
    return this._admin
  }

  // ===========================
  // Transaction API
  // ===========================

  transaction(): TransactionBuilder {
    return new TransactionBuilder(this.httpClient)
  }

  // ===========================
  // Direct ACK API
  // ===========================

  async ack(message: QueenMessage | QueenMessage[] | string | string[], status: boolean | string = true, context: AckContext = {}): Promise<AckResult> {
    const isBatch = Array.isArray(message)
    logger.log('Queen.ack', { isBatch, count: isBatch ? message.length : 1, status, context })

    if (Array.isArray(message)) {
      if (message.length === 0) {
        return { processed: 0, results: [], success: true }
      }

      const hasIndividualStatus = message.some(msg =>
        typeof msg === 'object' && msg !== null && ('_status' in msg || '_error' in msg)
      )

      let acknowledgments: Array<{
        transactionId: string
        partitionId: string
        status: string
        error: string | null
        leaseId?: string
      }>

      if (hasIndividualStatus) {
        acknowledgments = message.map(msg => {
          const transactionId = typeof msg === 'string' ? msg : ((msg as QueenMessage).transactionId || (msg as QueenMessage).id)
          const partitionId = typeof msg === 'object' ? (msg as QueenMessage).partitionId : null
          const leaseId = typeof msg === 'object' ? (msg as QueenMessage).leaseId : null

          if (!transactionId) {
            throw new Error('Message must have transactionId or id property')
          }

          if (!partitionId) {
            throw new Error('Message must have partitionId property to ensure message uniqueness')
          }

          const msgStatus = (msg as QueenMessage)._status !== undefined ? (msg as QueenMessage)._status : status
          const statusStr = typeof msgStatus === 'boolean'
            ? (msgStatus ? 'completed' : 'failed')
            : msgStatus as string

          const ack: { transactionId: string; partitionId: string; status: string; error: string | null; leaseId?: string } = {
            transactionId,
            partitionId,
            status: statusStr,
            error: (msg as QueenMessage)._error || context.error || null
          }

          if (leaseId) ack.leaseId = leaseId

          return ack
        })
      } else {
        const statusStr = typeof status === 'boolean'
          ? (status ? 'completed' : 'failed')
          : status

        acknowledgments = message.map(msg => {
          const transactionId = typeof msg === 'string' ? msg : ((msg as QueenMessage).transactionId || (msg as QueenMessage).id)
          const partitionId = typeof msg === 'object' ? (msg as QueenMessage).partitionId : null
          const leaseId = typeof msg === 'object' ? (msg as QueenMessage).leaseId : null

          if (!transactionId) {
            throw new Error('Message must have transactionId or id property')
          }

          if (!partitionId) {
            throw new Error('Message must have partitionId property to ensure message uniqueness')
          }

          const ack: { transactionId: string; partitionId: string; status: string; error: string | null; leaseId?: string } = {
            transactionId,
            partitionId,
            status: statusStr,
            error: context.error || null
          }

          if (leaseId) ack.leaseId = leaseId

          return ack
        })
      }

      try {
        const result = await this.httpClient.post('/api/v1/ack/batch', {
          acknowledgments,
          consumerGroup: context.group || null
        }) as AckResult | null

        if (result && result.error) {
          logger.error('Queen.ack', { type: 'batch', error: result.error })
          return { success: false, error: result.error }
        }

        logger.log('Queen.ack', { type: 'batch', success: true, count: acknowledgments.length })
        return { success: true, ...result }
      } catch (error) {
        logger.error('Queen.ack', { type: 'batch', error: (error as Error).message })
        return { success: false, error: (error as Error).message }
      }
    }

    // Handle single message acknowledgment
    const transactionId = typeof message === 'string' ? message : ((message as QueenMessage).transactionId || (message as QueenMessage).id)
    const partitionId = typeof message === 'object' ? (message as QueenMessage).partitionId : null
    const leaseId = typeof message === 'object' ? (message as QueenMessage).leaseId : null

    if (!transactionId) {
      return { success: false, error: 'Message must have transactionId or id property' }
    }

    if (!partitionId) {
      return { success: false, error: 'Message must have partitionId property to ensure message uniqueness' }
    }

    const statusStr = typeof status === 'boolean'
      ? (status ? 'completed' : 'failed')
      : status

    const body: { transactionId: string; partitionId: string; status: string; error: string | null; consumerGroup: string | null; leaseId?: string } = {
      transactionId,
      partitionId,
      status: statusStr,
      error: context.error || null,
      consumerGroup: context.group || null
    }

    if (leaseId) body.leaseId = leaseId

    try {
      const result = await this.httpClient.post('/api/v1/ack', body) as AckResult | null

      if (result && result.error) {
        logger.error('Queen.ack', { type: 'single', transactionId, error: result.error })
        return { success: false, error: result.error }
      }

      logger.log('Queen.ack', { type: 'single', transactionId, success: true })
      return { success: true, ...result }
    } catch (error) {
      logger.error('Queen.ack', { type: 'single', transactionId, error: (error as Error).message })
      return { success: false, error: (error as Error).message }
    }
  }

  // ===========================
  // Lease Renewal API
  // ===========================

  async renew(messageOrLeaseId: string | QueenMessage | (string | QueenMessage)[]): Promise<RenewResult | RenewResult[]> {
    let leaseIds: string[] = []

    if (typeof messageOrLeaseId === 'string') {
      leaseIds = [messageOrLeaseId]
    } else if (Array.isArray(messageOrLeaseId)) {
      leaseIds = messageOrLeaseId.map(item =>
        typeof item === 'string' ? item : (item as QueenMessage).leaseId!
      ).filter(Boolean)
    } else if (messageOrLeaseId && typeof messageOrLeaseId === 'object') {
      if ((messageOrLeaseId as QueenMessage).leaseId) {
        leaseIds = [(messageOrLeaseId as QueenMessage).leaseId!]
      }
    }

    if (leaseIds.length === 0) {
      logger.warn('Queen.renew', 'No valid lease IDs found for renewal')
      return { leaseId: '', success: false, error: 'No valid lease IDs found for renewal' }
    }

    logger.log('Queen.renew', { count: leaseIds.length })

    const results: RenewResult[] = []
    for (const leaseId of leaseIds) {
      try {
        const result = await this.httpClient.post(`/api/v1/lease/${leaseId}/extend`, {}) as { leaseId?: string; newExpiresAt?: string; lease_expires_at?: string }
        results.push({
          leaseId,
          success: true,
          newExpiresAt: result.leaseId ? result.newExpiresAt : result.lease_expires_at
        })
        logger.log('Queen.renew', { leaseId, success: true })
      } catch (error) {
        results.push({ leaseId, success: false, error: (error as Error).message })
        logger.error('Queen.renew', { leaseId, error: (error as Error).message })
      }
    }

    logger.log('Queen.renew', { total: results.length, successful: results.filter(r => r.success).length })
    return Array.isArray(messageOrLeaseId) ? results : results[0]
  }

  // ===========================
  // Buffer Management API
  // ===========================

  async flushAllBuffers(): Promise<void> {
    logger.log('Queen.flushAllBuffers', 'Starting flush of all buffers')
    await this.bufferManager.flushAllBuffers()
    logger.log('Queen.flushAllBuffers', 'Completed')
  }

  getBufferStats(): BufferStats {
    const stats = this.bufferManager.getStats()
    logger.log('Queen.getBufferStats', stats)
    return stats
  }

  // ===========================
  // Consumer Group Management
  // ===========================

  async deleteConsumerGroup(consumerGroup: string, deleteMetadata: boolean = true): Promise<unknown> {
    logger.log('Queen.deleteConsumerGroup', { consumerGroup, deleteMetadata })

    const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}?deleteMetadata=${deleteMetadata}`
    const response = await this.httpClient.delete(url)

    logger.log('Queen.deleteConsumerGroup', { success: true, consumerGroup })
    return response
  }

  async updateConsumerGroupTimestamp(consumerGroup: string, timestamp: string): Promise<unknown> {
    logger.log('Queen.updateConsumerGroupTimestamp', { consumerGroup, timestamp })

    const url = `/api/v1/consumer-groups/${encodeURIComponent(consumerGroup)}/subscription`
    const response = await this.httpClient.post(url, {
      subscriptionTimestamp: timestamp
    })

    logger.log('Queen.updateConsumerGroupTimestamp', { success: true, consumerGroup })
    return response
  }

  // ===========================
  // Streaming API
  // ===========================

  stream(name: string, namespace?: string): StreamBuilder {
    logger.log('Queen.stream', { name, namespace })
    return new StreamBuilder(this.httpClient, this, name, namespace)
  }

  consumer(streamName: string, consumerGroup: string): StreamConsumer {
    logger.log('Queen.consumer', { streamName, consumerGroup })
    return new StreamConsumer(this.httpClient, this, streamName, consumerGroup)
  }

  // ===========================
  // Graceful Shutdown
  // ===========================

  async close(): Promise<void> {
    logger.log('Queen.close', 'Starting shutdown')
    console.log('Closing Queen client...')

    try {
      await this.bufferManager.flushAllBuffers()
      logger.log('Queen.close', 'All buffers flushed')
      console.log('All buffers flushed')
    } catch (error) {
      logger.error('Queen.close', { error: (error as Error).message, phase: 'buffer-flush' })
      console.warn('Error flushing buffers:', error)
    }

    this.bufferManager.cleanup()

    for (const cleanup of this.shutdownHandlers) {
      cleanup()
    }
    this.shutdownHandlers = []

    logger.log('Queen.close', 'Client closed successfully')
    console.log('Queen client closed')
  }
}
