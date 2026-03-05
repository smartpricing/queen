/**
 * Message buffer for a single queue
 */

import type { FormattedPushItem, ResolvedBufferOptions } from '../types'

export class MessageBuffer {
  private queueAddress: string
  private messages: FormattedPushItem[] = []
  private _options: ResolvedBufferOptions
  private flushCallback: (queueAddress: string) => void
  private timer: ReturnType<typeof setTimeout> | null = null
  private firstMessageTime: number | null = null
  private flushing: boolean = false

  constructor(queueAddress: string, options: ResolvedBufferOptions, flushCallback: (queueAddress: string) => void) {
    this.queueAddress = queueAddress
    this._options = options
    this.flushCallback = flushCallback
  }

  add(formattedMessage: FormattedPushItem): void {
    if (this.messages.length === 0) {
      this.firstMessageTime = Date.now()
      this.startTimer()
    }

    this.messages.push(formattedMessage)

    if (this.messages.length >= this._options.messageCount) {
      this.triggerFlush()
    }
  }

  private startTimer(): void {
    if (this.timer) return

    this.timer = setTimeout(() => {
      this.triggerFlush()
    }, this._options.timeMillis)
  }

  private triggerFlush(): void {
    if (this.flushing || this.messages.length === 0) return

    if (this.timer) {
      clearTimeout(this.timer)
      this.timer = null
    }

    this.flushCallback(this.queueAddress)
  }

  extractMessages(batchSize: number | null = null): FormattedPushItem[] {
    if (batchSize === null || batchSize >= this.messages.length) {
      const messages = [...this.messages]
      this.messages = []
      this.firstMessageTime = null
      this.flushing = false

      if (this.timer) {
        clearTimeout(this.timer)
        this.timer = null
      }

      return messages
    }

    const messages = this.messages.splice(0, batchSize)

    if (this.messages.length === 0) {
      this.firstMessageTime = null
      this.flushing = false

      if (this.timer) {
        clearTimeout(this.timer)
        this.timer = null
      }
    } else {
      // Messages remain after partial extraction - restart timer
      // to ensure they get flushed within timeMillis
      if (!this.timer) {
        this.firstMessageTime = Date.now()
        this.startTimer()
      }
    }

    return messages
  }

  setFlushing(value: boolean): void {
    this.flushing = value
  }

  forceFlush(): void {
    if (this.timer) {
      clearTimeout(this.timer)
      this.timer = null
    }
    this.triggerFlush()
  }

  cancelTimer(): void {
    if (this.timer) {
      clearTimeout(this.timer)
      this.timer = null
    }
  }

  get messageCount(): number {
    return this.messages.length
  }

  get options(): ResolvedBufferOptions {
    return this._options
  }

  get firstMessageAge(): number {
    return this.firstMessageTime ? Date.now() - this.firstMessageTime : 0
  }

  requeue(messages: FormattedPushItem[]): void {
    // Prepend messages back to the buffer (they were extracted from the front)
    this.messages.unshift(...messages)
    if (!this.firstMessageTime) {
      this.firstMessageTime = Date.now()
    }
    this.flushing = false
    // Restart timer if not running
    if (!this.timer && this.messages.length > 0) {
      this.startTimer()
    }
  }

  cleanup(): void {
    if (this.timer) {
      clearTimeout(this.timer)
      this.timer = null
    }
    this.messages = []
    this.firstMessageTime = null
    this.flushing = false
  }
}
