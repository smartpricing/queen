/**
 * Queue builder for fluent API
 */

import { v7 as uuidv7 } from 'uuid'
import { isValidUUID } from '../utils/validation'
import { QUEUE_DEFAULTS, CONSUME_DEFAULTS, POP_DEFAULTS } from '../utils/defaults'
import * as logger from '../utils/logger'
import type { HttpClient } from '../http/HttpClient'
import type { BufferManager } from '../buffer/BufferManager'
import type { Queen } from '../Queen'
import type { QueenMessage, QueueConfig, BufferOptions, FormattedPushItem, PushItem, PushResult, ConsumeOptions, DLQResult } from '../types'

export const generateUUID = (): string => {
  return uuidv7()
}

export class QueueBuilder {
  private queen: Queen
  private httpClient: HttpClient
  private bufferManager: BufferManager
  private queueName: string | null = null
  private _partition: string = 'Default'
  private _namespace: string | null = null
  private _task: string | null = null
  private _group: string | null = null
  private _config: QueueConfig = {}

  // Consume options
  private _concurrency: number = CONSUME_DEFAULTS.concurrency
  private _batch: number = CONSUME_DEFAULTS.batch
  private _limit: number | null = CONSUME_DEFAULTS.limit
  private _idleMillis: number | null = CONSUME_DEFAULTS.idleMillis
  private _autoAck: boolean = CONSUME_DEFAULTS.autoAck
  private _wait: boolean = CONSUME_DEFAULTS.wait
  private _timeoutMillis: number = CONSUME_DEFAULTS.timeoutMillis
  private _renewLease: boolean = CONSUME_DEFAULTS.renewLease
  private _renewLeaseIntervalMillis: number | null = CONSUME_DEFAULTS.renewLeaseIntervalMillis
  private _subscriptionMode: string | null = CONSUME_DEFAULTS.subscriptionMode
  private _subscriptionFrom: string | null = CONSUME_DEFAULTS.subscriptionFrom
  private _each: boolean = false

  // Buffer options
  private _bufferOptions: BufferOptions | null = null

  constructor(queen: Queen, httpClient: HttpClient, bufferManager: BufferManager, queueName: string | null = null) {
    this.queen = queen
    this.httpClient = httpClient
    this.bufferManager = bufferManager
    this.queueName = queueName
  }

  private getAffinityKey(): string | null {
    if (this.queueName) {
      return `${this.queueName}:${this._partition || '*'}:${this._group || '__QUEUE_MODE__'}`
    } else if (this._namespace || this._task) {
      return `${this._namespace || '*'}:${this._task || '*'}:${this._group || '__QUEUE_MODE__'}`
    }
    return null
  }

  namespace(name: string): this {
    this._namespace = name
    return this
  }

  task(name: string): this {
    this._task = name
    return this
  }

  config(options: QueueConfig): this {
    this._config = { ...QUEUE_DEFAULTS, ...options }
    return this
  }

  create(): OperationBuilder {
    const fullConfig = Object.keys(this._config).length > 0
      ? this._config
      : QUEUE_DEFAULTS

    const payload = {
      queue: this.queueName,
      namespace: this._namespace,
      task: this._task,
      options: fullConfig
    }

    logger.log('QueueBuilder.create', { queue: this.queueName, namespace: this._namespace, task: this._task })
    return new OperationBuilder(this.httpClient, 'POST', '/api/v1/configure', payload)
  }

  delete(): OperationBuilder {
    if (!this.queueName) {
      throw new Error('Queue name is required for delete operation')
    }

    logger.log('QueueBuilder.delete', { queue: this.queueName })
    return new OperationBuilder(
      this.httpClient,
      'DELETE',
      `/api/v1/resources/queues/${encodeURIComponent(this.queueName)}`,
      null
    )
  }

  partition(name: string): this {
    this._partition = name
    return this
  }

  buffer(options: BufferOptions): this {
    this._bufferOptions = options
    return this
  }

  push(payload: PushItem | PushItem[]): PushBuilder {
    if (!this.queueName) {
      throw new Error('Queue name is required for push operation')
    }

    logger.log('QueueBuilder.push', { queue: this.queueName, partition: this._partition, count: Array.isArray(payload) ? payload.length : 1, buffered: !!this._bufferOptions })

    const items = Array.isArray(payload) ? payload : [payload]
    const formattedItems: FormattedPushItem[] = items.map(item => {
      let payloadValue: unknown
      if (typeof item === 'object' && item !== null && 'data' in item) {
        payloadValue = item.data
      } else if (typeof item === 'object' && item !== null && 'payload' in item) {
        payloadValue = item.payload
      } else {
        payloadValue = item
      }

      const result: FormattedPushItem = {
        queue: this.queueName!,
        partition: this._partition,
        payload: payloadValue,
        transactionId: (item as PushItem).transactionId || generateUUID()
      }

      if ((item as PushItem).traceId && isValidUUID((item as PushItem).traceId!)) {
        result.traceId = (item as PushItem).traceId
      }

      return result
    })

    return new PushBuilder(this.httpClient, this.bufferManager, this.queueName, this._partition, formattedItems, this._bufferOptions)
  }

  group(name: string): this {
    this._group = name
    return this
  }

  concurrency(count: number): this {
    this._concurrency = Math.max(1, count)
    return this
  }

  batch(size: number): this {
    this._batch = Math.max(1, size)
    return this
  }

  limit(count: number): this {
    this._limit = count
    return this
  }

  idleMillis(millis: number): this {
    this._idleMillis = millis
    return this
  }

  autoAck(enabled: boolean): this {
    this._autoAck = enabled
    return this
  }

  renewLease(enabled: boolean, intervalMillis?: number): this {
    this._renewLease = enabled
    if (intervalMillis) {
      this._renewLeaseIntervalMillis = intervalMillis
    }
    return this
  }

  subscriptionMode(mode: string): this {
    this._subscriptionMode = mode
    return this
  }

  subscriptionFrom(from: string): this {
    this._subscriptionFrom = from
    return this
  }

  each(): this {
    this._each = true
    return this
  }

  consume(handler: (msgOrMsgs: QueenMessage | QueenMessage[]) => Promise<unknown>, options: { signal?: AbortSignal } = {}): ConsumeBuilder {
    const consumeOptions: ConsumeOptions = {
      queue: this.queueName,
      partition: this._partition !== 'Default' ? this._partition : null,
      namespace: this._namespace,
      task: this._task,
      group: this._group,
      concurrency: this._concurrency,
      batch: this._batch,
      limit: this._limit,
      idleMillis: this._idleMillis,
      autoAck: this._autoAck,
      wait: this._wait,
      timeoutMillis: this._timeoutMillis,
      renewLease: this._renewLease,
      renewLeaseIntervalMillis: this._renewLeaseIntervalMillis,
      subscriptionMode: this._subscriptionMode,
      subscriptionFrom: this._subscriptionFrom,
      each: this._each,
      signal: options.signal
    }

    return new ConsumeBuilder(this.httpClient, this.queen, handler, consumeOptions)
  }

  wait(enabled: boolean): this {
    this._wait = enabled
    return this
  }

  async pop(): Promise<QueenMessage[]> {
    logger.log('QueueBuilder.pop', { queue: this.queueName, partition: this._partition, namespace: this._namespace, task: this._task, batch: this._batch, wait: this._wait, group: this._group })

    try {
      const path = this.buildPopPath()

      const effectiveAutoAck = this._autoAck !== CONSUME_DEFAULTS.autoAck ? this._autoAck : POP_DEFAULTS.autoAck

      const params = new URLSearchParams({
        batch: this._batch.toString(),
        wait: this._wait.toString(),
        timeout: this._timeoutMillis.toString()
      })

      if (this._group) params.append('consumerGroup', this._group)
      if (this._namespace) params.append('namespace', this._namespace)
      if (this._task) params.append('task', this._task)
      if (effectiveAutoAck) params.append('autoAck', 'true')
      if (this._subscriptionMode) params.append('subscriptionMode', this._subscriptionMode)
      if (this._subscriptionFrom) params.append('subscriptionFrom', this._subscriptionFrom)

      const affinityKey = this.getAffinityKey()

      const result = await this.httpClient.get(`${path}?${params}`, this._timeoutMillis + 5000, affinityKey) as { messages?: QueenMessage[] } | null

      if (!result || !result.messages) {
        logger.log('QueueBuilder.pop', { status: 'no-messages' })
        return []
      }

      const messages = result.messages.filter((msg): msg is QueenMessage => msg != null)
      logger.log('QueueBuilder.pop', { status: 'success', count: messages.length })
      return messages
    } catch (error) {
      logger.error('QueueBuilder.pop', { error: (error as Error).message })
      console.warn('Pop failed:', (error as Error).message)
      return []
    }
  }

  private buildPopPath(): string {
    if (this.queueName) {
      if (this._partition && this._partition !== 'Default') {
        return `/api/v1/pop/queue/${this.queueName}/partition/${this._partition}`
      }
      return `/api/v1/pop/queue/${this.queueName}`
    }

    if (this._namespace || this._task) {
      return '/api/v1/pop'
    }

    throw new Error('Must specify queue, namespace, or task for pop operation')
  }

  async flushBuffer(): Promise<void> {
    if (!this.queueName) {
      throw new Error('Queue name is required for buffer flush')
    }
    const queueAddress = `${this.queueName}/${this._partition}`
    logger.log('QueueBuilder.flushBuffer', { queueAddress })
    await this.bufferManager.flushBuffer(queueAddress)
  }

  dlq(consumerGroup: string | null = null): DLQBuilder {
    if (!this.queueName) {
      throw new Error('Queue name is required for DLQ operations')
    }
    logger.log('QueueBuilder.dlq', { queue: this.queueName, consumerGroup, partition: this._partition })
    return new DLQBuilder(this.httpClient, this.queueName, consumerGroup, this._partition)
  }
}

/**
 * Operation builder for create/delete operations with callbacks
 */
export class OperationBuilder {
  private httpClient: HttpClient
  private method: string
  private path: string
  private body: unknown
  private onSuccessCallback: ((result: unknown) => Promise<void> | void) | null = null
  private onErrorCallback: ((error: Error) => Promise<void> | void) | null = null
  private executed: boolean = false

  constructor(httpClient: HttpClient, method: string, path: string, body: unknown) {
    this.httpClient = httpClient
    this.method = method
    this.path = path
    this.body = body
  }

  onSuccess(callback: (result: unknown) => Promise<void> | void): this {
    this.onSuccessCallback = callback
    return this
  }

  onError(callback: (error: Error) => Promise<void> | void): this {
    this.onErrorCallback = callback
    return this
  }

  then<TResult1 = unknown, TResult2 = never>(
    resolve?: ((value: unknown) => TResult1 | PromiseLike<TResult1>) | null,
    reject?: ((reason: unknown) => TResult2 | PromiseLike<TResult2>) | null
  ): Promise<TResult1 | TResult2> {
    if (this.executed) {
      return Promise.resolve().then(resolve, reject) as Promise<TResult1 | TResult2>
    }
    this.executed = true
    return this.execute().then(resolve, reject)
  }

  private async execute(): Promise<unknown> {
    logger.log('OperationBuilder.execute', { method: this.method, path: this.path })

    try {
      let result: unknown

      if (this.method === 'GET') {
        result = await this.httpClient.get(this.path)
      } else if (this.method === 'POST') {
        result = await this.httpClient.post(this.path, this.body)
      } else if (this.method === 'PUT') {
        result = await this.httpClient.put(this.path, this.body)
      } else if (this.method === 'DELETE') {
        result = await this.httpClient.delete(this.path)
      }

      if (result && typeof result === 'object' && 'error' in (result as Record<string, unknown>)) {
        const error = new Error((result as { error: string }).error)
        logger.error('OperationBuilder.execute', { method: this.method, path: this.path, error: (result as { error: string }).error })
        if (this.onErrorCallback) {
          await this.onErrorCallback(error)
          return { success: false, error: (result as { error: string }).error }
        }
        throw error
      }

      logger.log('OperationBuilder.execute', { method: this.method, path: this.path, status: 'success' })
      if (this.onSuccessCallback) {
        await this.onSuccessCallback(result)
      }

      return result

    } catch (error) {
      logger.error('OperationBuilder.execute', { method: this.method, path: this.path, error: (error as Error).message })
      if (this.onErrorCallback) {
        await this.onErrorCallback(error as Error)
        return { success: false, error: (error as Error).message }
      }
      throw error
    }
  }
}

/**
 * Consume builder for chaining callbacks
 */
export class ConsumeBuilder {
  private httpClient: HttpClient
  private queen: Queen
  private handler: (msgOrMsgs: QueenMessage | QueenMessage[]) => Promise<unknown>
  private options: ConsumeOptions
  private onSuccessCallback: ((msgOrMsgs: QueenMessage | QueenMessage[], result: unknown) => Promise<void> | void) | null = null
  private onErrorCallback: ((msgOrMsgs: QueenMessage | QueenMessage[], error: Error) => Promise<void> | void) | null = null
  private executed: boolean = false

  constructor(httpClient: HttpClient, queen: Queen, handler: (msgOrMsgs: QueenMessage | QueenMessage[]) => Promise<unknown>, options: ConsumeOptions) {
    this.httpClient = httpClient
    this.queen = queen
    this.handler = handler
    this.options = options
  }

  onSuccess(callback: (msgOrMsgs: QueenMessage | QueenMessage[], result: unknown) => Promise<void> | void): this {
    this.onSuccessCallback = callback
    return this
  }

  onError(callback: (msgOrMsgs: QueenMessage | QueenMessage[], error: Error) => Promise<void> | void): this {
    this.onErrorCallback = callback
    return this
  }

  then<TResult1 = unknown, TResult2 = never>(
    resolve?: ((value: unknown) => TResult1 | PromiseLike<TResult1>) | null,
    reject?: ((reason: unknown) => TResult2 | PromiseLike<TResult2>) | null
  ): Promise<TResult1 | TResult2> {
    if (this.executed) {
      return Promise.resolve().then(resolve, reject) as Promise<TResult1 | TResult2>
    }
    this.executed = true
    return this.executeConsume().then(resolve, reject)
  }

  private async executeConsume(): Promise<void> {
    const { ConsumerManager } = await import('../consumer/ConsumerManager')
    const consumerManager = new ConsumerManager(this.httpClient, this.queen)

    const wrappedHandler = async (msgOrMsgs: QueenMessage | QueenMessage[]) => {
      try {
        const result = await this.handler(msgOrMsgs)

        if (this.onSuccessCallback) {
          await this.onSuccessCallback(msgOrMsgs, result)
        }

        return result
      } catch (error) {
        if (this.onErrorCallback) {
          await this.onErrorCallback(msgOrMsgs, error as Error)
          return
        }
        throw error
      }
    }

    const hasCallbacks = this.onSuccessCallback || this.onErrorCallback
    const effectiveAutoAck = hasCallbacks ? false : this.options.autoAck

    const updatedOptions: ConsumeOptions = {
      ...this.options,
      autoAck: effectiveAutoAck
    }

    return consumerManager.start(wrappedHandler, updatedOptions)
  }
}

/**
 * Push builder for chaining callbacks
 */
export class PushBuilder {
  private httpClient: HttpClient
  private bufferManager: BufferManager
  private queueName: string
  private _partition: string
  private formattedItems: FormattedPushItem[]
  private _bufferOptions: BufferOptions | null
  private onSuccessCallback: ((items: FormattedPushItem[]) => Promise<void> | void) | null = null
  private onErrorCallback: ((items: FormattedPushItem[], error: Error) => Promise<void> | void) | null = null
  private onDuplicateCallback: ((items: (FormattedPushItem & { result: PushResult })[], error: Error) => Promise<void> | void) | null = null
  private executed: boolean = false

  constructor(httpClient: HttpClient, bufferManager: BufferManager, queueName: string, partition: string, formattedItems: FormattedPushItem[], bufferOptions: BufferOptions | null) {
    this.httpClient = httpClient
    this.bufferManager = bufferManager
    this.queueName = queueName
    this._partition = partition
    this.formattedItems = formattedItems
    this._bufferOptions = bufferOptions
  }

  onSuccess(callback: (items: FormattedPushItem[]) => Promise<void> | void): this {
    this.onSuccessCallback = callback
    return this
  }

  onError(callback: (items: FormattedPushItem[], error: Error) => Promise<void> | void): this {
    this.onErrorCallback = callback
    return this
  }

  onDuplicate(callback: (items: (FormattedPushItem & { result: PushResult })[], error: Error) => Promise<void> | void): this {
    this.onDuplicateCallback = callback
    return this
  }

  then<TResult1 = unknown, TResult2 = never>(
    resolve?: ((value: unknown) => TResult1 | PromiseLike<TResult1>) | null,
    reject?: ((reason: unknown) => TResult2 | PromiseLike<TResult2>) | null
  ): Promise<TResult1 | TResult2> {
    if (this.executed) {
      return Promise.resolve().then(resolve, reject) as Promise<TResult1 | TResult2>
    }
    this.executed = true
    return this.executePush().then(resolve, reject)
  }

  private async executePush(): Promise<unknown> {
    logger.log('PushBuilder.execute', { queue: this.queueName, partition: this._partition, count: this.formattedItems.length, buffered: !!this._bufferOptions })

    // Client-side buffering
    if (this._bufferOptions) {
      for (const item of this.formattedItems) {
        const queueAddress = `${this.queueName}/${this._partition}`
        this.bufferManager.addMessage(queueAddress, item, this._bufferOptions)
      }
      const result = { buffered: true, count: this.formattedItems.length }

      logger.log('PushBuilder.execute', { status: 'buffered', count: this.formattedItems.length })

      if (this.onSuccessCallback) {
        await this.onSuccessCallback(this.formattedItems)
      }

      return result
    }

    // Immediate push
    try {
      const results = await this.httpClient.post('/api/v1/push', { items: this.formattedItems })

      if (Array.isArray(results)) {
        const successful: (FormattedPushItem & { result: PushResult })[] = []
        const duplicates: (FormattedPushItem & { result: PushResult })[] = []
        const failed: (FormattedPushItem & { result: PushResult; error?: string })[] = []

        for (let i = 0; i < results.length; i++) {
          const result = results[i] as PushResult
          const originalItem = this.formattedItems[i]

          if (result.status === 'duplicate') {
            duplicates.push({ ...originalItem, result })
          } else if (result.status === 'failed') {
            failed.push({ ...originalItem, result, error: result.error })
          } else if (result.status === 'queued') {
            successful.push({ ...originalItem, result })
          }
        }

        if (duplicates.length > 0 && this.onDuplicateCallback) {
          await this.onDuplicateCallback(duplicates, new Error('Duplicate transaction IDs detected'))
        }

        if (failed.length > 0 && this.onErrorCallback) {
          const error = new Error(failed[0].error || 'Push failed')
          await this.onErrorCallback(failed, error)
        }

        if (successful.length > 0 && this.onSuccessCallback) {
          await this.onSuccessCallback(successful)
        }

        if (failed.length > 0 && !this.onErrorCallback) {
          logger.error('PushBuilder.execute', { status: 'failed', count: failed.length })
          throw new Error(failed[0].error || 'Push failed')
        }

        logger.log('PushBuilder.execute', { status: 'success', successful: successful.length, duplicates: duplicates.length, failed: failed.length })
        return results
      }

      // Fallback for non-array responses
      if (results && typeof results === 'object' && 'error' in (results as Record<string, unknown>)) {
        const error = new Error((results as { error: string }).error)
        if (this.onErrorCallback) {
          await this.onErrorCallback(this.formattedItems, error)
          return results
        }
        throw error
      }

      if (this.onSuccessCallback) {
        await this.onSuccessCallback(this.formattedItems)
      }

      return results

    } catch (error) {
      if (this.onErrorCallback) {
        await this.onErrorCallback(this.formattedItems, error as Error)
        return null
      }
      throw error
    }
  }
}

/**
 * DLQ (Dead Letter Queue) builder for querying failed messages
 */
export class DLQBuilder {
  private httpClient: HttpClient
  private queueName: string
  private consumerGroup: string | null
  private _partition: string | null
  private _limit: number = 100
  private _offset: number = 0
  private _from: string | null = null
  private _to: string | null = null

  constructor(httpClient: HttpClient, queueName: string, consumerGroup: string | null, partition: string) {
    this.httpClient = httpClient
    this.queueName = queueName
    this.consumerGroup = consumerGroup
    this._partition = partition !== 'Default' ? partition : null
  }

  limit(count: number): this {
    this._limit = Math.max(1, count)
    return this
  }

  offset(count: number): this {
    this._offset = Math.max(0, count)
    return this
  }

  from(timestamp: string): this {
    this._from = timestamp
    return this
  }

  to(timestamp: string): this {
    this._to = timestamp
    return this
  }

  async get(): Promise<DLQResult> {
    const params = new URLSearchParams()

    params.append('queue', this.queueName)
    params.append('limit', this._limit.toString())
    params.append('offset', this._offset.toString())

    if (this.consumerGroup) {
      params.append('consumerGroup', this.consumerGroup)
    }

    if (this._partition) {
      params.append('partition', this._partition)
    }

    if (this._from) {
      params.append('from', this._from)
    }

    if (this._to) {
      params.append('to', this._to)
    }

    logger.log('DLQBuilder.get', { queue: this.queueName, consumerGroup: this.consumerGroup, partition: this._partition, limit: this._limit, offset: this._offset })

    try {
      const result = await this.httpClient.get(`/api/v1/dlq?${params}`) as DLQResult | null
      logger.log('DLQBuilder.get', { status: 'success', total: result?.total || 0, messages: result?.messages?.length || 0 })
      return result || { messages: [], total: 0 }
    } catch (error) {
      logger.error('DLQBuilder.get', { error: (error as Error).message })
      console.warn('DLQ query failed:', (error as Error).message)
      return { messages: [], total: 0 }
    }
  }
}
