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
      logger.debug('BufferManager.flushBuffer', { queueAddress, status: 'empty' })
      return
    }

    logger.log('BufferManager.flushBuffer', { queueAddress, messageCount: buffer.messageCount })
    buffer.setFlushing(true)

    // Create a promise for this flush and track it
    const flushPromise = (async () => {
      try {
        const messages = buffer.extractMessages()

        logger.debug('BufferManager.flushBuffer', { queueAddress, extracted: messages.length })

        if (messages.length === 0) return

        // Send to server
        const result = await this.#httpClient.post('/api/v1/push', { items: messages })
        logger.debug('BufferManager.flushBuffer', { queueAddress, serverResponse: result ? `${result.length || 'N/A'} items` : 'null' })

        this.#flushCount++
        logger.log('BufferManager.flushBuffer', { queueAddress, status: 'success', messagesSent: messages.length })

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

        logger.debug('BufferManager.flushBufferBatch', { queueAddress, extracted: messages.length })

        if (messages.length === 0) return

        // Send to server
        const result = await this.#httpClient.post('/api/v1/push', { items: messages })
        logger.debug('BufferManager.flushBufferBatch', { queueAddress, serverResponse: result ? `${result.length || 'N/A'} items` : 'null' })

        this.#flushCount++

        // Remove empty buffer if no more messages
        if (buffer.messageCount === 0) {
          this.#buffers.delete(queueAddress)
        } else {
          buffer.setFlushing(false)
        }

      } catch (error) {
        logger.error('BufferManager.flushBufferBatch', { queueAddress, error: error.message })
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
      logger.debug('BufferManager.flushBuffer', { queueAddress, status: 'not-found' })
      await this.#waitForPendingFlushes()
      return
    }

    // Cancel timer to prevent time-based flush
    buffer.cancelTimer()
    
    // Get the batch size from buffer options
    const batchSize = buffer.options.messageCount
    
    // Flush all messages in batches
    while (buffer.messageCount > 0) {
      logger.debug('BufferManager.flushBuffer', { queueAddress, batchSize, remaining: buffer.messageCount })
      await this.#flushBufferBatch(queueAddress, batchSize)
    }
    
    // Wait for all pending flushes to complete
    await this.#waitForPendingFlushes()
    
    logger.debug('BufferManager.flushBuffer', { queueAddress, status: 'completed' })
  }

  async flushAllBuffers() {
    // Get all queue addresses that have buffers
    const queueAddresses = Array.from(this.#buffers.keys())
    logger.log('BufferManager.flushAllBuffers', { bufferCount: queueAddresses.length, pendingFlushes: this.#pendingFlushes.size })
    
    // Flush each buffer in batches
    for (const queueAddress of queueAddresses) {
      await this.flushBuffer(queueAddress)
    }
    
    logger.log('BufferManager.flushAllBuffers', { status: 'completed' })
  }

  async #waitForPendingFlushes() {
    if (this.#pendingFlushes.size === 0) return
    
    logger.debug('BufferManager.waitForPendingFlushes', { count: this.#pendingFlushes.size })
    await Promise.all(Array.from(this.#pendingFlushes))
    logger.debug('BufferManager.waitForPendingFlushes', { status: 'completed' })
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
