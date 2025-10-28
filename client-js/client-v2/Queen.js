/**
 * Queen Message Queue Client - Version 2
 * Clean, fluent API with smart defaults
 */

import { HttpClient } from './http/HttpClient.js'
import { LoadBalancer } from './http/LoadBalancer.js'
import { BufferManager } from './buffer/BufferManager.js'
import { QueueBuilder } from './builders/QueueBuilder.js'
import { TransactionBuilder } from './builders/TransactionBuilder.js'
import { CLIENT_DEFAULTS } from './utils/defaults.js'
import { validateUrl, validateUrls } from './utils/validation.js'
import * as logger from './utils/logger.js'

export class Queen {
  #httpClient
  #bufferManager
  #config
  #shutdownHandlers = []

  constructor(config = {}) {
    logger.log('Queen.constructor', { config: typeof config === 'object' && !Array.isArray(config) ? { ...config, urls: config.urls?.length || 0 } : { type: typeof config } })
    
    // Normalize config
    this.#config = this.#normalizeConfig(config)

    // Create HTTP client
    this.#httpClient = this.#createHttpClient()

    // Create buffer manager
    this.#bufferManager = new BufferManager(this.#httpClient)

    // Setup graceful shutdown
    this.#setupGracefulShutdown()
    
    logger.log('Queen.constructor', { status: 'initialized', urls: this.#config.urls.length })
  }

  #normalizeConfig(config) {
    // Handle different input formats
    if (typeof config === 'string') {
      // Single URL string
      return {
        ...CLIENT_DEFAULTS,
        urls: [validateUrl(config)]
      }
    }

    if (Array.isArray(config)) {
      // Array of URLs
      return {
        ...CLIENT_DEFAULTS,
        urls: validateUrls(config)
      }
    }

    // Object config
    const normalized = {
      ...CLIENT_DEFAULTS,
      ...config
    }

    // Ensure URLs are validated
    if (normalized.urls) {
      normalized.urls = validateUrls(normalized.urls)
    } else if (normalized.url) {
      normalized.urls = [validateUrl(normalized.url)]
    } else {
      throw new Error('Must provide urls or url in configuration')
    }

    return normalized
  }

  #createHttpClient() {
    const { urls, timeoutMillis, retryAttempts, retryDelayMillis, loadBalancingStrategy, enableFailover } = this.#config

    if (urls.length === 1) {
      // Single server
      return new HttpClient({
        baseUrl: urls[0],
        timeoutMillis,
        retryAttempts,
        retryDelayMillis
      })
    }

    // Multiple servers with load balancing
    const loadBalancer = new LoadBalancer(urls, loadBalancingStrategy)
    return new HttpClient({
      loadBalancer,
      timeoutMillis,
      retryAttempts,
      retryDelayMillis,
      enableFailover
    })
  }

  #setupGracefulShutdown() {
    let signalReceivedCount = 0;
    const shutdown = async (signal) => {
      console.log(`\nReceived ${signal}, shutting down gracefully...`)
      try {
        signalReceivedCount++;
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

    // Store handlers for cleanup
    this.#shutdownHandlers.push(
      () => process.off('SIGINT', sigintHandler),
      () => process.off('SIGTERM', sigtermHandler)
    )
  }

  // ===========================
  // Queue Builder Entry Point
  // ===========================

  queue(name = null) {
    return new QueueBuilder(this, this.#httpClient, this.#bufferManager, name)
  }

  // ===========================
  // Transaction API
  // ===========================

  transaction() {
    return new TransactionBuilder(this.#httpClient)
  }

  // ===========================
  // Direct ACK API
  // ===========================

  async ack(message, status = true, context = {}) {
    const isBatch = Array.isArray(message)
    logger.log('Queen.ack', { isBatch, count: isBatch ? message.length : 1, status, context })
    
    // Handle batch acknowledgment
    if (Array.isArray(message)) {
      if (message.length === 0) {
        return { processed: 0, results: [] }
      }

      // Check if messages have individual status
      const hasIndividualStatus = message.some(msg =>
        typeof msg === 'object' && msg !== null && ('_status' in msg || '_error' in msg)
      )

      let acknowledgments

      if (hasIndividualStatus) {
        // Each message has its own status
        acknowledgments = message.map(msg => {
          const transactionId = typeof msg === 'string' ? msg : (msg.transactionId || msg.id)
          const partitionId = typeof msg === 'object' ? msg.partitionId : null
          const leaseId = typeof msg === 'object' ? msg.leaseId : null

          if (!transactionId) {
            throw new Error('Message must have transactionId or id property')
          }

          // CRITICAL: partitionId is now MANDATORY to prevent acking wrong message
          if (!partitionId) {
            throw new Error('Message must have partitionId property to ensure message uniqueness')
          }

          const msgStatus = msg._status !== undefined ? msg._status : status
          const statusStr = typeof msgStatus === 'boolean'
            ? (msgStatus ? 'completed' : 'failed')
            : msgStatus

          const ack = {
            transactionId,
            partitionId,
            status: statusStr,
            error: msg._error || context.error || null
          }

          if (leaseId) ack.leaseId = leaseId

          return ack
        })
      } else {
        // Same status for all messages
        const statusStr = typeof status === 'boolean'
          ? (status ? 'completed' : 'failed')
          : status

        acknowledgments = message.map(msg => {
          const transactionId = typeof msg === 'string' ? msg : (msg.transactionId || msg.id)
          const partitionId = typeof msg === 'object' ? msg.partitionId : null
          const leaseId = typeof msg === 'object' ? msg.leaseId : null

          if (!transactionId) {
            throw new Error('Message must have transactionId or id property')
          }

          // CRITICAL: partitionId is now MANDATORY to prevent acking wrong message
          if (!partitionId) {
            throw new Error('Message must have partitionId property to ensure message uniqueness')
          }

          const ack = {
            transactionId,
            partitionId,
            status: statusStr,
            error: context.error || null
          }

          if (leaseId) ack.leaseId = leaseId

          return ack
        })
      }

      // Call batch ack endpoint
      try {
        const result = await this.#httpClient.post('/api/v1/ack/batch', {
          acknowledgments,
          consumerGroup: context.group || null
        })

        if (result && result.error) {
          logger.error('Queen.ack', { type: 'batch', error: result.error })
          return { success: false, error: result.error }
        }

        logger.log('Queen.ack', { type: 'batch', success: true, count: acknowledgments.length })
        return { success: true, ...result }
      } catch (error) {
        logger.error('Queen.ack', { type: 'batch', error: error.message })
        return { success: false, error: error.message }
      }
    }

    // Handle single message acknowledgment
    const transactionId = typeof message === 'string' ? message : (message.transactionId || message.id)
    const partitionId = typeof message === 'object' ? message.partitionId : null
    const leaseId = typeof message === 'object' ? message.leaseId : null

    if (!transactionId) {
      return { success: false, error: 'Message must have transactionId or id property' }
    }

    // CRITICAL: partitionId is now MANDATORY to prevent acking wrong message
    if (!partitionId) {
      return { success: false, error: 'Message must have partitionId property to ensure message uniqueness' }
    }

    const statusStr = typeof status === 'boolean'
      ? (status ? 'completed' : 'failed')
      : status

    const body = {
      transactionId,
      partitionId,
      status: statusStr,
      error: context.error || null,
      consumerGroup: context.group || null
    }

    if (leaseId) body.leaseId = leaseId

    try {
      const result = await this.#httpClient.post('/api/v1/ack', body)

      if (result && result.error) {
        logger.error('Queen.ack', { type: 'single', transactionId, error: result.error })
        return { success: false, error: result.error }
      }

      logger.log('Queen.ack', { type: 'single', transactionId, success: true })
      return { success: true, ...result }
    } catch (error) {
      logger.error('Queen.ack', { type: 'single', transactionId, error: error.message })
      return { success: false, error: error.message }
    }
  }

  // ===========================
  // Lease Renewal API
  // ===========================

  async renew(messageOrLeaseId) {
    let leaseIds = []

    if (typeof messageOrLeaseId === 'string') {
      leaseIds = [messageOrLeaseId]
    } else if (Array.isArray(messageOrLeaseId)) {
      leaseIds = messageOrLeaseId.map(item =>
        typeof item === 'string' ? item : item.leaseId
      ).filter(Boolean)
    } else if (messageOrLeaseId && typeof messageOrLeaseId === 'object') {
      if (messageOrLeaseId.leaseId) {
        leaseIds = [messageOrLeaseId.leaseId]
      }
    }

    if (leaseIds.length === 0) {
      logger.warn('Queen.renew', 'No valid lease IDs found for renewal')
      return { success: false, error: 'No valid lease IDs found for renewal' }
    }
    
    logger.log('Queen.renew', { count: leaseIds.length })

    const results = []
    for (const leaseId of leaseIds) {
      try {
        const result = await this.#httpClient.post(`/api/v1/lease/${leaseId}/extend`, {})
        results.push({
          leaseId,
          success: true,
          newExpiresAt: result.leaseId ? result.newExpiresAt : result.lease_expires_at
        })
        logger.log('Queen.renew', { leaseId, success: true })
      } catch (error) {
        results.push({ leaseId, success: false, error: error.message })
        logger.error('Queen.renew', { leaseId, error: error.message })
      }
    }

    logger.log('Queen.renew', { total: results.length, successful: results.filter(r => r.success).length })
    return Array.isArray(messageOrLeaseId) ? results : results[0]
  }

  // ===========================
  // Buffer Management API
  // ===========================

  async flushAllBuffers() {
    logger.log('Queen.flushAllBuffers', 'Starting flush of all buffers')
    await this.#bufferManager.flushAllBuffers()
    logger.log('Queen.flushAllBuffers', 'Completed')
  }

  getBufferStats() {
    const stats = this.#bufferManager.getStats()
    logger.log('Queen.getBufferStats', stats)
    return stats
  }

  // ===========================
  // Graceful Shutdown
  // ===========================

  async close() {
    logger.log('Queen.close', 'Starting shutdown')
    console.log('Closing Queen client...')

    // Flush all buffers
    try {
      await this.#bufferManager.flushAllBuffers()
      logger.log('Queen.close', 'All buffers flushed')
      console.log('All buffers flushed')
    } catch (error) {
      logger.error('Queen.close', { error: error.message, phase: 'buffer-flush' })
      console.warn('Error flushing buffers:', error)
    }

    // Cleanup buffer manager
    this.#bufferManager.cleanup()

    // Remove shutdown handlers
    for (const cleanup of this.#shutdownHandlers) {
      cleanup()
    }
    this.#shutdownHandlers = []

    logger.log('Queen.close', 'Client closed successfully')
    console.log('Queen client closed')
  }
}

