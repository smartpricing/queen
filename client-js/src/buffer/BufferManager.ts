/**
 * Buffer manager for client-side message buffering across queues
 */

import { MessageBuffer } from './MessageBuffer'
import { BUFFER_DEFAULTS } from '../utils/defaults'
import * as logger from '../utils/logger'
import type { HttpClient } from '../http/HttpClient'
import type { FormattedPushItem, BufferOptions, BufferStats } from '../types'

export class BufferManager {
  private httpClient: HttpClient
  private buffers: Map<string, MessageBuffer> = new Map()
  private pendingFlushes: Set<Promise<void>> = new Set()
  private flushCount: number = 0

  constructor(httpClient: HttpClient) {
    this.httpClient = httpClient
  }

  addMessage(queueAddress: string, formattedMessage: FormattedPushItem, bufferOptions?: BufferOptions): void {
    const options = { ...BUFFER_DEFAULTS, ...bufferOptions }

    if (!this.buffers.has(queueAddress)) {
      logger.log('BufferManager.createBuffer', { queueAddress, options })
      this.buffers.set(queueAddress, new MessageBuffer(
        queueAddress,
        options,
        (addr) => this.flushBufferInternal(addr)
      ))
    }

    const buffer = this.buffers.get(queueAddress)!
    buffer.add(formattedMessage)
    logger.log('BufferManager.addMessage', { queueAddress, messageCount: buffer.messageCount })
  }

  private async flushBufferInternal(queueAddress: string): Promise<void> {
    const buffer = this.buffers.get(queueAddress)
    if (!buffer || buffer.messageCount === 0) {
      logger.log('BufferManager.flushBuffer', { queueAddress, status: 'empty' })
      return
    }

    logger.log('BufferManager.flushBuffer', { queueAddress, messageCount: buffer.messageCount })
    buffer.setFlushing(true)

    let flushPromise!: Promise<void>
    flushPromise = (async () => {
      try {
        const messages = buffer.extractMessages()

        if (messages.length === 0) return

        try {
          await this.httpClient.post('/api/v1/push', { items: messages })
          logger.log('BufferManager.flushBuffer', { queueAddress, status: 'success', messagesSent: messages.length })
        } catch (sendError) {
          // Re-add messages to buffer on failure to prevent data loss
          buffer.requeue(messages)
          logger.error('BufferManager.flushBuffer', { queueAddress, error: (sendError as Error).message, requeuedCount: messages.length })
          throw sendError
        }

        this.flushCount++

        this.buffers.delete(queueAddress)

      } catch (error) {
        logger.error('BufferManager.flushBuffer', { queueAddress, error: (error as Error).message })
        buffer.setFlushing(false)
        throw error
      } finally {
        this.pendingFlushes.delete(flushPromise)
      }
    })()

    this.pendingFlushes.add(flushPromise)

    return flushPromise
  }

  private async flushBufferBatch(queueAddress: string, batchSize: number): Promise<void> {
    const buffer = this.buffers.get(queueAddress)
    if (!buffer || buffer.messageCount === 0) {
      return
    }

    buffer.setFlushing(true)

    let flushPromise!: Promise<void>
    flushPromise = (async () => {
      try {
        const messages = buffer.extractMessages(batchSize)

        if (messages.length === 0) return

        try {
          await this.httpClient.post('/api/v1/push', { items: messages })
          logger.log('BufferManager.flushBufferBatch', { queueAddress, status: 'success', messagesSent: messages.length })
        } catch (sendError) {
          // Re-add messages to buffer on failure to prevent data loss
          buffer.requeue(messages)
          logger.error('BufferManager.flushBufferBatch', { queueAddress, error: (sendError as Error).message, requeuedCount: messages.length })
          throw sendError
        }

        this.flushCount++

        if (buffer.messageCount === 0) {
          this.buffers.delete(queueAddress)
        } else {
          buffer.setFlushing(false)
        }

      } catch (error) {
        buffer.setFlushing(false)
        throw error
      } finally {
        this.pendingFlushes.delete(flushPromise)
      }
    })()

    this.pendingFlushes.add(flushPromise)

    return flushPromise
  }

  async flushBuffer(queueAddress: string): Promise<void> {
    logger.log('BufferManager.flushBuffer', { queueAddress, activeBuffers: this.buffers.size, pendingFlushes: this.pendingFlushes.size })

    const buffer = this.buffers.get(queueAddress)
    if (!buffer) {
      logger.log('BufferManager.flushBuffer', { queueAddress, status: 'not-found' })
      await this.waitForPendingFlushes()
      return
    }

    buffer.cancelTimer()

    const batchSize = buffer.options.messageCount

    while (buffer.messageCount > 0) {
      await this.flushBufferBatch(queueAddress, batchSize)
    }

    await this.waitForPendingFlushes()
  }

  async flushAllBuffers(): Promise<void> {
    const queueAddresses = Array.from(this.buffers.keys())
    logger.log('BufferManager.flushAllBuffers', { bufferCount: queueAddresses.length, pendingFlushes: this.pendingFlushes.size })

    for (const queueAddress of queueAddresses) {
      await this.flushBuffer(queueAddress)
    }

    logger.log('BufferManager.flushAllBuffers', { status: 'completed' })
  }

  private async waitForPendingFlushes(): Promise<void> {
    if (this.pendingFlushes.size === 0) return

    await Promise.all(Array.from(this.pendingFlushes))
  }

  getStats(): BufferStats {
    let totalBufferedMessages = 0
    let oldestBufferAge = 0

    for (const buffer of this.buffers.values()) {
      totalBufferedMessages += buffer.messageCount
      const age = buffer.firstMessageAge
      oldestBufferAge = Math.max(oldestBufferAge, age)
    }

    const stats: BufferStats = {
      activeBuffers: this.buffers.size,
      totalBufferedMessages,
      oldestBufferAge,
      flushesPerformed: this.flushCount
    }

    logger.log('BufferManager.getStats', stats)
    return stats
  }

  cleanup(): void {
    logger.log('BufferManager.cleanup', { bufferCount: this.buffers.size })
    for (const buffer of this.buffers.values()) {
      buffer.cleanup()
    }
    this.buffers.clear()
  }
}
