/**
 * Buffer manager for client-side message buffering across queues
 */

import { MessageBuffer } from './MessageBuffer.js'
import { BUFFER_DEFAULTS } from '../utils/defaults.js'

export class BufferManager {
  #httpClient
  #buffers = new Map() // queueAddress -> MessageBuffer
  #flushCount = 0

  constructor(httpClient) {
    this.#httpClient = httpClient
  }

  addMessage(queueAddress, formattedMessage, bufferOptions) {
    const options = { ...BUFFER_DEFAULTS, ...bufferOptions }

    if (!this.#buffers.has(queueAddress)) {
      this.#buffers.set(queueAddress, new MessageBuffer(
        queueAddress,
        options,
        (addr) => this.#flushBuffer(addr)
      ))
    }

    const buffer = this.#buffers.get(queueAddress)
    buffer.add(formattedMessage)
  }

  async #flushBuffer(queueAddress) {
    const buffer = this.#buffers.get(queueAddress)
    if (!buffer || buffer.messageCount === 0) {
      return
    }

    buffer.setFlushing(true)

    try {
      const messages = buffer.extractMessages()

      if (messages.length === 0) return

      // Send to server
      await this.#httpClient.post('/api/v1/push', { items: messages })

      this.#flushCount++

      // Remove empty buffer
      this.#buffers.delete(queueAddress)

    } catch (error) {
      buffer.setFlushing(false)
      throw error
    }
  }

  async flushBuffer(queueAddress) {
    return this.#flushBuffer(queueAddress)
  }

  async flushAllBuffers() {
    const flushPromises = []
    for (const queueAddress of this.#buffers.keys()) {
      flushPromises.push(this.#flushBuffer(queueAddress))
    }
    await Promise.all(flushPromises)
  }

  getStats() {
    let totalBufferedMessages = 0
    let oldestBufferAge = 0

    for (const buffer of this.#buffers.values()) {
      totalBufferedMessages += buffer.messageCount
      const age = buffer.firstMessageAge
      oldestBufferAge = Math.max(oldestBufferAge, age)
    }

    return {
      activeBuffers: this.#buffers.size,
      totalBufferedMessages,
      oldestBufferAge,
      flushesPerformed: this.#flushCount
    }
  }

  cleanup() {
    for (const buffer of this.#buffers.values()) {
      buffer.cleanup()
    }
    this.#buffers.clear()
  }
}

