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
      console.log(`No buffer or empty buffer for ${queueAddress}`)
      return
    }

    logger.log('BufferManager.flushBuffer', { queueAddress, messageCount: buffer.messageCount })
    console.log(`Flushing ${buffer.messageCount} messages for ${queueAddress}`)
    buffer.setFlushing(true)

    // Create a promise for this flush and track it
    const flushPromise = (async () => {
      try {
        const messages = buffer.extractMessages()

        console.log(`Extracted ${messages.length} messages, sending to server...`)

        if (messages.length === 0) return

        // Send to server
        const result = await this.#httpClient.post('/api/v1/push', { items: messages })
        console.log(`Server responded:`, result ? `${result.length || 'N/A'} items` : 'null')

        this.#flushCount++
        logger.log('BufferManager.flushBuffer', { queueAddress, status: 'success', messagesSent: messages.length })

        // Remove empty buffer
        this.#buffers.delete(queueAddress)

      } catch (error) {
        logger.error('BufferManager.flushBuffer', { queueAddress, error: error.message })
        console.error(`Flush error for ${queueAddress}:`, error.message)
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

        console.log(`Extracted ${messages.length} messages, sending to server...`)

        if (messages.length === 0) return

        // Send to server
        const result = await this.#httpClient.post('/api/v1/push', { items: messages })
        console.log(`Server responded:`, result ? `${result.length || 'N/A'} items` : 'null')

        this.#flushCount++

        // Remove empty buffer if no more messages
        if (buffer.messageCount === 0) {
          this.#buffers.delete(queueAddress)
        } else {
          buffer.setFlushing(false)
        }

      } catch (error) {
        console.error(`Flush error for ${queueAddress}:`, error.message)
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
    console.log(`flushBuffer called for address: ${queueAddress}`)
    console.log(`Active buffers:`, Array.from(this.#buffers.keys()))
    console.log(`Pending flushes:`, this.#pendingFlushes.size)
    
    const buffer = this.#buffers.get(queueAddress)
    if (!buffer) {
      logger.log('BufferManager.flushBuffer', { queueAddress, status: 'not-found' })
      console.log(`No buffer found for ${queueAddress}`)
      await this.#waitForPendingFlushes()
      return
    }

    // Cancel timer to prevent time-based flush
    buffer.cancelTimer()
    
    // Get the batch size from buffer options
    const batchSize = buffer.options.messageCount
    
    // Flush all messages in batches
    while (buffer.messageCount > 0) {
      console.log(`Flushing batch of up to ${batchSize} messages (${buffer.messageCount} remaining)`)
      await this.#flushBufferBatch(queueAddress, batchSize)
    }
    
    // Wait for all pending flushes to complete
    await this.#waitForPendingFlushes()
    
    console.log(`flushBuffer completed for ${queueAddress}`)
  }

  async flushAllBuffers() {
    const queueAddresses = Array.from(this.#buffers.keys())
    logger.log('BufferManager.flushAllBuffers', { bufferCount: queueAddresses.length, pendingFlushes: this.#pendingFlushes.size })
    console.log(`flushAllBuffers called, pending flushes: ${this.#pendingFlushes.size}`)
    
    // Get all queue addresses that have buffers
    
    // Flush each buffer in batches
    for (const queueAddress of queueAddresses) {
      await this.flushBuffer(queueAddress)
    }
    
    logger.log('BufferManager.flushAllBuffers', { status: 'completed' })
    console.log(`flushAllBuffers completed`)
  }

  async #waitForPendingFlushes() {
    if (this.#pendingFlushes.size === 0) return
    
    console.log(`Waiting for ${this.#pendingFlushes.size} pending flushes...`)
    await Promise.all(Array.from(this.#pendingFlushes))
    console.log(`All pending flushes completed`)
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

