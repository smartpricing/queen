/**
 * Buffer manager for client-side message buffering across queues
 */

import { MessageBuffer } from './MessageBuffer.js'
import { BUFFER_DEFAULTS } from '../utils/defaults.js'
import * as logger from '../utils/logger.js'

export class BufferManager {
  #httpClient
  #buffers = new Map() // queueAddress -> MessageBuffer
  #pendingFlushes = new Set() // Track in-flight flush promises
  #flushCount = 0

  constructor(httpClient) {
    this.#httpClient = httpClient
  }

  addMessage(queueAddress, formattedMessage, bufferOptions) {
    const options = { ...BUFFER_DEFAULTS, ...bufferOptions }

    if (!this.#buffers.has(queueAddress)) {
      logger.log('BufferManager.createBuffer', { queueAddress, options })
      this.#buffers.set(queueAddress, new MessageBuffer(
        queueAddress,
        options,
        (addr) => this.#flushBuffer(addr)
      ))
    }

    const buffer = this.#buffers.get(queueAddress)
    buffer.add(formattedMessage)
    logger.log('BufferManager.addMessage', { queueAddress, messageCount: buffer.messageCount })
  }

  async #flushBuffer(queueAddress) {
    const buffer = this.#buffers.get(queueAddress)
    if (!buffer || buffer.messageCount === 0) {
      logger.log('BufferManager.flushBuffer', { queueAddress, status: 'empty' })
      return
    }

    logger.log('BufferManager.flushBuffer', { queueAddress, messageCount: buffer.messageCount })
    buffer.setFlushing(true)

    // Create a promise for this flush and track it
    const flushPromise = (async () => {
      try {
        const messages = buffer.extractMessages()

        if (messages.length === 0) return

        try {
          // Send to server
          await this.#httpClient.post('/api/v1/push', { items: messages })
          logger.log('BufferManager.flushBuffer', { queueAddress, status: 'success', messagesSent: messages.length })
        } catch (sendError) {
          // Re-add messages to buffer on failure to prevent data loss
          buffer.requeue(messages)
          logger.error('BufferManager.flushBuffer', { queueAddress, error: sendError.message, requeuedCount: messages.length })
          throw sendError
        }

        this.#flushCount++

        // Remove empty buffer
        this.#buffers.delete(queueAddress)

      } catch (error) {
        logger.error('BufferManager.flushBuffer', { queueAddress, error: error.message })
        buffer.setFlushing(false)
        throw error
      } finally {
        // Remove from pending flushes
        this.#pendingFlushes.delete(flushPromise)
      }
    })()

    // Track this flush
    this.#pendingFlushes.add(flushPromise)

    return flushPromise
  }

  async #flushBufferBatch(queueAddress, batchSize) {
    const buffer = this.#buffers.get(queueAddress)
    if (!buffer || buffer.messageCount === 0) {
      return
    }

    buffer.setFlushing(true)

    // Create a promise for this flush and track it
    const flushPromise = (async () => {
      try {
        const messages = buffer.extractMessages(batchSize)

        if (messages.length === 0) return

        try {
          // Send to server
          await this.#httpClient.post('/api/v1/push', { items: messages })
          logger.log('BufferManager.flushBufferBatch', { queueAddress, status: 'success', messagesSent: messages.length })
        } catch (sendError) {
          // Re-add messages to buffer on failure to prevent data loss
          buffer.requeue(messages)
          logger.error('BufferManager.flushBufferBatch', { queueAddress, error: sendError.message, requeuedCount: messages.length })
          throw sendError
        }

        this.#flushCount++

        // Remove empty buffer if no more messages
        if (buffer.messageCount === 0) {
          this.#buffers.delete(queueAddress)
        } else {
          buffer.setFlushing(false)
        }

      } catch (error) {
        buffer.setFlushing(false)
        throw error
      } finally {
        // Remove from pending flushes
        this.#pendingFlushes.delete(flushPromise)
      }
    })()

    // Track this flush
    this.#pendingFlushes.add(flushPromise)

    return flushPromise
  }

  async flushBuffer(queueAddress) {
    logger.log('BufferManager.flushBuffer', { queueAddress, activeBuffers: this.#buffers.size, pendingFlushes: this.#pendingFlushes.size })

    const buffer = this.#buffers.get(queueAddress)
    if (!buffer) {
      logger.log('BufferManager.flushBuffer', { queueAddress, status: 'not-found' })
      await this.#waitForPendingFlushes()
      return
    }

    // Cancel timer to prevent time-based flush
    buffer.cancelTimer()
    
    // Get the batch size from buffer options
    const batchSize = buffer.options.messageCount
    
    // Flush all messages in batches
    while (buffer.messageCount > 0) {
      await this.#flushBufferBatch(queueAddress, batchSize)
    }

    // Wait for all pending flushes to complete
    await this.#waitForPendingFlushes()
  }

  async flushAllBuffers() {
    const queueAddresses = Array.from(this.#buffers.keys())
    logger.log('BufferManager.flushAllBuffers', { bufferCount: queueAddresses.length, pendingFlushes: this.#pendingFlushes.size })

    for (const queueAddress of queueAddresses) {
      await this.flushBuffer(queueAddress)
    }

    logger.log('BufferManager.flushAllBuffers', { status: 'completed' })
  }

  async #waitForPendingFlushes() {
    if (this.#pendingFlushes.size === 0) return
    
    await Promise.all(Array.from(this.#pendingFlushes))
  }

  getStats() {
    let totalBufferedMessages = 0
    let oldestBufferAge = 0

    for (const buffer of this.#buffers.values()) {
      totalBufferedMessages += buffer.messageCount
      const age = buffer.firstMessageAge
      oldestBufferAge = Math.max(oldestBufferAge, age)
    }

    const stats = {
      activeBuffers: this.#buffers.size,
      totalBufferedMessages,
      oldestBufferAge,
      flushesPerformed: this.#flushCount
    }
    
    logger.log('BufferManager.getStats', stats)
    return stats
  }

  cleanup() {
    logger.log('BufferManager.cleanup', { bufferCount: this.#buffers.size })
    for (const buffer of this.#buffers.values()) {
      buffer.cleanup()
    }
    this.#buffers.clear()
  }
}

