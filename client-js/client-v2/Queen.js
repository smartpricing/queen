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

export class Queen {
  #httpClient
  #bufferManager
  #config
  #shutdownHandlers = []

  constructor(config = {}) {
    // Normalize config
    this.#config = this.#normalizeConfig(config)

    // Create HTTP client
    this.#httpClient = this.#createHttpClient()

    // Create buffer manager
    this.#bufferManager = new BufferManager(this.#httpClient)

    // Setup graceful shutdown
    this.#setupGracefulShutdown()
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
    const shutdown = async (signal) => {
      console.log(`\nReceived ${signal}, shutting down gracefully...`)
      try {
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

          const msgStatus = msg._status !== undefined ? msg._status : status
          const statusStr = typeof msgStatus === 'boolean'
            ? (msgStatus ? 'completed' : 'failed')
            : msgStatus

          const ack = {
            transactionId,
            status: statusStr,
            error: msg._error || context.error || null
          }

          if (partitionId) ack.partitionId = partitionId
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

          const ack = {
            transactionId,
            status: statusStr,
            error: context.error || null
          }

          if (partitionId) ack.partitionId = partitionId
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
          return { success: false, error: result.error }
        }

        return { success: true, ...result }
      } catch (error) {
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

    const statusStr = typeof status === 'boolean'
      ? (status ? 'completed' : 'failed')
      : status

    const body = {
      transactionId,
      status: statusStr,
      error: context.error || null,
      consumerGroup: context.group || null
    }

    if (partitionId) body.partitionId = partitionId
    if (leaseId) body.leaseId = leaseId

    try {
      const result = await this.#httpClient.post('/api/v1/ack', body)

      if (result && result.error) {
        return { success: false, error: result.error }
      }

      return { success: true, ...result }
    } catch (error) {
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
      return { success: false, error: 'No valid lease IDs found for renewal' }
    }

    const results = []
    for (const leaseId of leaseIds) {
      try {
        const result = await this.#httpClient.post(`/api/v1/lease/${leaseId}/extend`, {})
        results.push({
          leaseId,
          success: true,
          newExpiresAt: result.leaseId ? result.newExpiresAt : result.lease_expires_at
        })
      } catch (error) {
        results.push({ leaseId, success: false, error: error.message })
      }
    }

    return Array.isArray(messageOrLeaseId) ? results : results[0]
  }

  // ===========================
  // Buffer Management API
  // ===========================

  async flushAllBuffers() {
    await this.#bufferManager.flushAllBuffers()
  }

  getBufferStats() {
    return this.#bufferManager.getStats()
  }

  // ===========================
  // Graceful Shutdown
  // ===========================

  async close() {
    console.log('Closing Queen client...')

    // Flush all buffers
    try {
      await this.#bufferManager.flushAllBuffers()
      console.log('All buffers flushed')
    } catch (error) {
      console.warn('Error flushing buffers:', error)
    }

    // Cleanup buffer manager
    this.#bufferManager.cleanup()

    // Remove shutdown handlers
    for (const cleanup of this.#shutdownHandlers) {
      cleanup()
    }
    this.#shutdownHandlers = []

    console.log('Queen client closed')
  }
}

