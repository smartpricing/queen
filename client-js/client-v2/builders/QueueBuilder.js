/**
 * Queue builder for fluent API
 */

import { generateUUID } from '../../utils/uuid.js'
import { isValidUUID } from '../utils/validation.js'
import { QUEUE_DEFAULTS, CONSUME_DEFAULTS, POP_DEFAULTS } from '../utils/defaults.js'

export class QueueBuilder {
  #queen
  #httpClient
  #bufferManager
  #queueName = null
  #partition = 'Default'
  #namespace = null
  #task = null
  #group = null
  #config = {}

  // Consume options
  #concurrency = CONSUME_DEFAULTS.concurrency
  #batch = CONSUME_DEFAULTS.batch
  #limit = CONSUME_DEFAULTS.limit
  #idleMillis = CONSUME_DEFAULTS.idleMillis
  #autoAck = CONSUME_DEFAULTS.autoAck
  #wait = CONSUME_DEFAULTS.wait
  #timeoutMillis = CONSUME_DEFAULTS.timeoutMillis
  #renewLease = CONSUME_DEFAULTS.renewLease
  #renewLeaseIntervalMillis = CONSUME_DEFAULTS.renewLeaseIntervalMillis
  #subscriptionMode = CONSUME_DEFAULTS.subscriptionMode
  #subscriptionFrom = CONSUME_DEFAULTS.subscriptionFrom
  #each = false

  // Buffer options
  #bufferOptions = null

  constructor(queen, httpClient, bufferManager, queueName = null) {
    this.#queen = queen
    this.#httpClient = httpClient
    this.#bufferManager = bufferManager
    this.#queueName = queueName
  }

  // ===========================
  // Queue Configuration Methods
  // ===========================

  namespace(name) {
    this.#namespace = name
    return this
  }

  task(name) {
    this.#task = name
    return this
  }

  config(options) {
    this.#config = { ...QUEUE_DEFAULTS, ...options }
    return this
  }

  create() {
    const payload = {
      queue: this.#queueName,
      namespace: this.#namespace,
      task: this.#task,
      options: this.#config
    }

    return new OperationBuilder(this.#httpClient, 'POST', '/api/v1/configure', payload)
  }

  delete() {
    if (!this.#queueName) {
      throw new Error('Queue name is required for delete operation')
    }

    return new OperationBuilder(
      this.#httpClient,
      'DELETE',
      `/api/v1/resources/queues/${encodeURIComponent(this.#queueName)}`,
      null
    )
  }

  // ===========================
  // Push Methods
  // ===========================

  partition(name) {
    this.#partition = name
    return this
  }

  buffer(options) {
    this.#bufferOptions = options
    return this
  }

  push(payload) {
    if (!this.#queueName) {
      throw new Error('Queue name is required for push operation')
    }

    // Format items
    const items = Array.isArray(payload) ? payload : [payload]
    const formattedItems = items.map(item => {
      const result = {
        queue: this.#queueName,
        partition: this.#partition,
        payload: item.payload || item,
        transactionId: item.transactionId || generateUUID()
      }

      // Include traceId if provided and valid UUID
      const traceId = item.traceId
      if (traceId && isValidUUID(traceId)) {
        result.traceId = traceId
      }

      return result
    })

    // Return a PushBuilder for chaining callbacks
    return new PushBuilder(this.#httpClient, this.#bufferManager, this.#queueName, this.#partition, formattedItems, this.#bufferOptions)
  }

  // ===========================
  // Consume Configuration Methods
  // ===========================

  group(name) {
    this.#group = name
    return this
  }

  concurrency(count) {
    this.#concurrency = Math.max(1, count)
    return this
  }

  batch(size) {
    this.#batch = Math.max(1, size)
    return this
  }

  limit(count) {
    this.#limit = count
    return this
  }

  idleMillis(millis) {
    this.#idleMillis = millis
    return this
  }

  autoAck(enabled) {
    this.#autoAck = enabled
    return this
  }

  renewLease(enabled, intervalMillis) {
    this.#renewLease = enabled
    if (intervalMillis) {
      this.#renewLeaseIntervalMillis = intervalMillis
    }
    return this
  }

  subscriptionMode(mode) {
    this.#subscriptionMode = mode
    return this
  }

  subscriptionFrom(from) {
    this.#subscriptionFrom = from
    return this
  }

  each() {
    this.#each = true
    return this
  }

  // ===========================
  // Consume Method
  // ===========================

  consume(handler, options = {}) {
    const consumeOptions = {
      queue: this.#queueName,
      partition: this.#partition !== 'Default' ? this.#partition : null,
      namespace: this.#namespace,
      task: this.#task,
      group: this.#group,
      concurrency: this.#concurrency,
      batch: this.#batch,
      limit: this.#limit,
      idleMillis: this.#idleMillis,
      autoAck: this.#autoAck,
      wait: this.#wait,
      timeoutMillis: this.#timeoutMillis,
      renewLease: this.#renewLease,
      renewLeaseIntervalMillis: this.#renewLeaseIntervalMillis,
      subscriptionMode: this.#subscriptionMode,
      subscriptionFrom: this.#subscriptionFrom,
      each: this.#each,
      signal: options.signal
    }

    return new ConsumeBuilder(this.#httpClient, this.#queen, handler, consumeOptions)
  }

  // ===========================
  // Pop Methods
  // ===========================

  wait(enabled) {
    this.#wait = enabled
    return this
  }

  async pop() {
    try {
      const path = this.#buildPopPath()
      const params = this.#buildPopParams()

      const result = await this.#httpClient.get(`${path}?${params}`, this.#timeoutMillis + 5000)

      if (!result || !result.messages) {
        return []
      }

      return result.messages.filter(msg => msg != null)
    } catch (error) {
      // Return empty array on error instead of throwing
      console.warn('Pop failed:', error.message)
      return []
    }
  }

  #buildPopPath() {
    if (this.#queueName) {
      if (this.#partition && this.#partition !== 'Default') {
        return `/api/v1/pop/queue/${this.#queueName}/partition/${this.#partition}`
      }
      return `/api/v1/pop/queue/${this.#queueName}`
    }

    if (this.#namespace || this.#task) {
      return '/api/v1/pop'
    }

    throw new Error('Must specify queue, namespace, or task for pop operation')
  }

  #buildPopParams() {
    const params = new URLSearchParams({
      batch: this.#batch.toString(),
      wait: this.#wait.toString(),
      timeout: this.#timeoutMillis.toString()  // Server expects 'timeout', not 'timeoutMillis'
    })

    if (this.#group) params.append('consumerGroup', this.#group)
    if (this.#namespace) params.append('namespace', this.#namespace)
    if (this.#task) params.append('task', this.#task)
    if (this.#autoAck) params.append('autoAck', 'true')
    if (this.#subscriptionMode) params.append('subscriptionMode', this.#subscriptionMode)
    if (this.#subscriptionFrom) params.append('subscriptionFrom', this.#subscriptionFrom)

    return params
  }

  // ===========================
  // Buffer Management Methods
  // ===========================

  async flushBuffer() {
    if (!this.#queueName) {
      throw new Error('Queue name is required for buffer flush')
    }
    const queueAddress = `${this.#queueName}/${this.#partition}`
    await this.#bufferManager.flushBuffer(queueAddress)
  }
}

/**
 * Operation builder for create/delete operations with callbacks
 */
class OperationBuilder {
  #httpClient
  #method
  #path
  #body
  #onSuccessCallback = null
  #onErrorCallback = null
  #executed = false

  constructor(httpClient, method, path, body) {
    this.#httpClient = httpClient
    this.#method = method
    this.#path = path
    this.#body = body
  }

  onSuccess(callback) {
    this.#onSuccessCallback = callback
    return this
  }

  onError(callback) {
    this.#onErrorCallback = callback
    return this
  }

  // Auto-execute when awaited
  then(resolve, reject) {
    if (this.#executed) {
      return Promise.resolve().then(resolve, reject)
    }
    this.#executed = true
    return this.#execute().then(resolve, reject)
  }

  async #execute() {
    try {
      let result

      if (this.#method === 'GET') {
        result = await this.#httpClient.get(this.#path)
      } else if (this.#method === 'POST') {
        result = await this.#httpClient.post(this.#path, this.#body)
      } else if (this.#method === 'PUT') {
        result = await this.#httpClient.put(this.#path, this.#body)
      } else if (this.#method === 'DELETE') {
        result = await this.#httpClient.delete(this.#path)
      }

      if (result && result.error) {
        const error = new Error(result.error)
        if (this.#onErrorCallback) {
          await this.#onErrorCallback(error)
          return { success: false, error: result.error }
        }
        throw error
      }

      if (this.#onSuccessCallback) {
        await this.#onSuccessCallback(result)
      }

      return result

    } catch (error) {
      if (this.#onErrorCallback) {
        await this.#onErrorCallback(error)
        return { success: false, error: error.message }
      }
      throw error
    }
  }
}

/**
 * Consume builder for chaining callbacks
 */
class ConsumeBuilder {
  #httpClient
  #queen
  #handler
  #options
  #onSuccessCallback = null
  #onErrorCallback = null
  #executed = false

  constructor(httpClient, queen, handler, options) {
    this.#httpClient = httpClient
    this.#queen = queen
    this.#handler = handler
    this.#options = options
  }

  onSuccess(callback) {
    this.#onSuccessCallback = callback
    return this
  }

  onError(callback) {
    this.#onErrorCallback = callback
    return this
  }

  // Auto-execute when awaited
  then(resolve, reject) {
    if (this.#executed) {
      return Promise.resolve().then(resolve, reject)
    }
    this.#executed = true
    return this.#execute().then(resolve, reject)
  }

  async #execute() {
    // Import ConsumerManager lazily to avoid circular dependency
    const { ConsumerManager } = await import('../consumer/ConsumerManager.js')
    const consumerManager = new ConsumerManager(this.#httpClient, this.#queen)

    // Wrap the handler to include callback logic
    const wrappedHandler = async (msgOrMsgs) => {
      try {
        const result = await this.#handler(msgOrMsgs)
        
        // Call onSuccess if defined
        if (this.#onSuccessCallback) {
          await this.#onSuccessCallback(msgOrMsgs, result)
        }
        
        return result
      } catch (error) {
        // Call onError if defined
        if (this.#onErrorCallback) {
          await this.#onErrorCallback(msgOrMsgs, error)
          // Don't re-throw if callback is defined
          return
        }
        // Re-throw if no error callback
        throw error
      }
    }

    // IMPORTANT: If callbacks are defined, auto-ack must be disabled
    // to prevent double-acking (auto-ack + manual ack in callback)
    const hasCallbacks = this.#onSuccessCallback || this.#onErrorCallback
    const effectiveAutoAck = hasCallbacks ? false : this.#options.autoAck

    const updatedOptions = {
      ...this.#options,
      autoAck: effectiveAutoAck
    }

    return consumerManager.start(wrappedHandler, updatedOptions)
  }
}

/**
 * Push builder for chaining callbacks
 */
class PushBuilder {
  #httpClient
  #bufferManager
  #queueName
  #partition
  #formattedItems
  #bufferOptions
  #onSuccessCallback = null
  #onErrorCallback = null
  #onDuplicateCallback = null
  #executed = false

  constructor(httpClient, bufferManager, queueName, partition, formattedItems, bufferOptions) {
    this.#httpClient = httpClient
    this.#bufferManager = bufferManager
    this.#queueName = queueName
    this.#partition = partition
    this.#formattedItems = formattedItems
    this.#bufferOptions = bufferOptions
  }

  onSuccess(callback) {
    this.#onSuccessCallback = callback
    return this
  }

  onError(callback) {
    this.#onErrorCallback = callback
    return this
  }

  onDuplicate(callback) {
    this.#onDuplicateCallback = callback
    return this
  }

  // Auto-execute when awaited
  then(resolve, reject) {
    if (this.#executed) {
      return Promise.resolve().then(resolve, reject)
    }
    this.#executed = true
    return this.#execute().then(resolve, reject)
  }

  async #execute() {
    // Client-side buffering
    if (this.#bufferOptions) {
      for (const item of this.#formattedItems) {
        const queueAddress = `${this.#queueName}/${this.#partition}`
        this.#bufferManager.addMessage(queueAddress, item, this.#bufferOptions)
      }
      const result = { buffered: true, count: this.#formattedItems.length }
      
      if (this.#onSuccessCallback) {
        await this.#onSuccessCallback(this.#formattedItems)
      }
      
      return result
    }

    // Immediate push
    try {
      const results = await this.#httpClient.post('/api/v1/push', { items: this.#formattedItems })

      // Server returns an array of results with status for each item
      if (Array.isArray(results)) {
        // Separate results by status
        const successful = []
        const duplicates = []
        const failed = []

        for (let i = 0; i < results.length; i++) {
          const result = results[i]
          const originalItem = this.#formattedItems[i]

          if (result.status === 'duplicate') {
            duplicates.push({ ...originalItem, result })
          } else if (result.status === 'failed') {
            failed.push({ ...originalItem, result, error: result.error })
          } else if (result.status === 'queued') {
            successful.push({ ...originalItem, result })
          }
        }

        // Call appropriate callbacks
        if (duplicates.length > 0 && this.#onDuplicateCallback) {
          await this.#onDuplicateCallback(duplicates, new Error('Duplicate transaction IDs detected'))
        }

        if (failed.length > 0 && this.#onErrorCallback) {
          const error = new Error(failed[0].error || 'Push failed')
          await this.#onErrorCallback(failed, error)
        }

        if (successful.length > 0 && this.#onSuccessCallback) {
          await this.#onSuccessCallback(successful)
        }

        // Only throw if no error callback is defined
        if (failed.length > 0 && !this.#onErrorCallback) {
          throw new Error(failed[0].error || 'Push failed')
        }

        return results
      }

      // Fallback for non-array responses
      if (results && results.error) {
        const error = new Error(results.error)
        if (this.#onErrorCallback) {
          await this.#onErrorCallback(this.#formattedItems, error)
          return results // Don't throw if callback is defined
        }
        throw error
      }

      if (this.#onSuccessCallback) {
        await this.#onSuccessCallback(this.#formattedItems)
      }

      return results

    } catch (error) {
      // Network or HTTP errors
      if (this.#onErrorCallback) {
        await this.#onErrorCallback(this.#formattedItems, error)
        return null // Don't throw if callback is defined
      }
      throw error
    }
  }
}

