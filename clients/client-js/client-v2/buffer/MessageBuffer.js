/**
 * Message buffer for a single queue
 */

export class MessageBuffer {
  #queueAddress
  #messages = []
  #options
  #flushCallback
  #timer = null
  #firstMessageTime = null
  #flushing = false

  constructor(queueAddress, options, flushCallback) {
    this.#queueAddress = queueAddress
    this.#options = options
    this.#flushCallback = flushCallback
  }

  add(formattedMessage) {
    // Set first message time if this is the first message
    if (this.#messages.length === 0) {
      this.#firstMessageTime = Date.now()
      this.#startTimer()
    }

    this.#messages.push(formattedMessage)

    // Check if we should flush based on size
    if (this.#messages.length >= this.#options.messageCount) {
      this.#triggerFlush()
    }
  }

  #startTimer() {
    if (this.#timer) return // Timer already running

    this.#timer = setTimeout(() => {
      this.#triggerFlush()
    }, this.#options.timeMillis)
  }

  #triggerFlush() {
    if (this.#flushing || this.#messages.length === 0) return

    // Clear timer
    if (this.#timer) {
      clearTimeout(this.#timer)
      this.#timer = null
    }

    // Trigger flush via callback
    this.#flushCallback(this.#queueAddress)
  }

  extractMessages(batchSize = null) {
    // If no batch size specified, extract all messages
    if (batchSize === null || batchSize >= this.#messages.length) {
      const messages = [...this.#messages]
      this.#messages = []
      this.#firstMessageTime = null
      this.#flushing = false

      if (this.#timer) {
        clearTimeout(this.#timer)
        this.#timer = null
      }

      return messages
    }

    // Extract a batch of messages
    const messages = this.#messages.splice(0, batchSize)
    
    // If buffer is now empty, reset state
    if (this.#messages.length === 0) {
      this.#firstMessageTime = null
      this.#flushing = false
      
      if (this.#timer) {
        clearTimeout(this.#timer)
        this.#timer = null
      }
    }

    return messages
  }

  setFlushing(value) {
    this.#flushing = value
  }

  forceFlush() {
    // Immediately trigger flush, ignoring timers
    if (this.#timer) {
      clearTimeout(this.#timer)
      this.#timer = null
    }
    this.#triggerFlush()
  }

  cancelTimer() {
    // Cancel the timer without triggering a flush
    if (this.#timer) {
      clearTimeout(this.#timer)
      this.#timer = null
    }
  }

  get messageCount() {
    return this.#messages.length
  }

  get options() {
    return this.#options
  }

  get firstMessageAge() {
    return this.#firstMessageTime ? Date.now() - this.#firstMessageTime : 0
  }

  cleanup() {
    if (this.#timer) {
      clearTimeout(this.#timer)
      this.#timer = null
    }
    this.#messages = []
    this.#firstMessageTime = null
    this.#flushing = false
  }
}

