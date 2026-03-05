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
      console.log(`No buffer or empty buffer for ${queueAddress}`)
      return
    }

    logger.log('BufferManager.flushBuffer', { queueAddress, messageCount: buffer.messageCount })
    console.log(`Flushing ${buffer.messageCount} messages for ${queueAddress}`)
    buffer.setFlushing(true)

    let flushPromise!: Promise<void>
    flushPromise = (async () => {
      try {
        const messages = buffer.extractMessages()

        console.log(`Extracted ${messages.length} messages, sending to server...`)

        if (messages.length === 0) return

        const result = await this.httpClient.post('/api/v1/push', { items: messages }) as { length?: number } | null
        console.log(`Server responded:`, result ? `${result.length || 'N/A'} items` : 'null')

        this.flushCount++
        logger.log('BufferManager.flushBuffer', { queueAddress, status: 'success', messagesSent: messages.length })

        this.buffers.delete(queueAddress)

      } catch (error) {
        logger.error('BufferManager.flushBuffer', { queueAddress, error: (error as Error).message })
        console.error(`Flush error for ${queueAddress}:`, (error as Error).message)
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

        console.log(`Extracted ${messages.length} messages, sending to server...`)

        if (messages.length === 0) return

        const result = await this.httpClient.post('/api/v1/push', { items: messages }) as { length?: number } | null
        console.log(`Server responded:`, result ? `${result.length || 'N/A'} items` : 'null')

        this.flushCount++

        if (buffer.messageCount === 0) {
          this.buffers.delete(queueAddress)
        } else {
          buffer.setFlushing(false)
        }

      } catch (error) {
        console.error(`Flush error for ${queueAddress}:`, (error as Error).message)
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
    console.log(`flushBuffer called for address: ${queueAddress}`)
    console.log(`Active buffers:`, Array.from(this.buffers.keys()))
    console.log(`Pending flushes:`, this.pendingFlushes.size)

    const buffer = this.buffers.get(queueAddress)
    if (!buffer) {
      logger.log('BufferManager.flushBuffer', { queueAddress, status: 'not-found' })
      console.log(`No buffer found for ${queueAddress}`)
      await this.waitForPendingFlushes()
      return
    }

    buffer.cancelTimer()

    const batchSize = buffer.options.messageCount

    while (buffer.messageCount > 0) {
      console.log(`Flushing batch of up to ${batchSize} messages (${buffer.messageCount} remaining)`)
      await this.flushBufferBatch(queueAddress, batchSize)
    }

    await this.waitForPendingFlushes()

    console.log(`flushBuffer completed for ${queueAddress}`)
  }

  async flushAllBuffers(): Promise<void> {
    const queueAddresses = Array.from(this.buffers.keys())
    logger.log('BufferManager.flushAllBuffers', { bufferCount: queueAddresses.length, pendingFlushes: this.pendingFlushes.size })
    console.log(`flushAllBuffers called, pending flushes: ${this.pendingFlushes.size}`)

    for (const queueAddress of queueAddresses) {
      await this.flushBuffer(queueAddress)
    }

    logger.log('BufferManager.flushAllBuffers', { status: 'completed' })
    console.log(`flushAllBuffers completed`)
  }

  private async waitForPendingFlushes(): Promise<void> {
    if (this.pendingFlushes.size === 0) return

    console.log(`Waiting for ${this.pendingFlushes.size} pending flushes...`)
    await Promise.all(Array.from(this.pendingFlushes))
    console.log(`All pending flushes completed`)
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
