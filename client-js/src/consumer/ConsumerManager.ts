/**
 * Consumer manager for handling concurrent workers
 */

import * as logger from '../utils/logger'
import type { HttpClient } from '../http/HttpClient'
import type { Queen } from '../Queen'
import type { ConsumeOptions, QueenMessage, TraceConfig, WorkerOptions } from '../types'

export class ConsumerManager {
  private httpClient: HttpClient
  private queen: Queen

  constructor(httpClient: HttpClient, queen: Queen) {
    this.httpClient = httpClient
    this.queen = queen
  }

  private getAffinityKey(queue: string | null, partition: string | null, namespace: string | null, task: string | null, group: string | null): string | null {
    if (queue) {
      return `${queue}:${partition || '*'}:${group || '__QUEUE_MODE__'}`
    } else if (namespace || task) {
      return `${namespace || '*'}:${task || '*'}:${group || '__QUEUE_MODE__'}`
    }
    return null
  }

  async start(handler: (msgOrMsgs: QueenMessage | QueenMessage[]) => Promise<unknown>, options: ConsumeOptions): Promise<void> {
    const {
      queue,
      partition,
      namespace,
      task,
      group,
      concurrency,
      batch,
      limit,
      idleMillis,
      autoAck,
      wait,
      timeoutMillis,
      renewLease,
      renewLeaseIntervalMillis,
      subscriptionMode,
      subscriptionFrom,
      each,
      signal
    } = options

    logger.log('ConsumerManager.start', {
      queue,
      partition,
      namespace,
      task,
      group,
      concurrency,
      batch,
      limit,
      autoAck,
      wait,
      each
    })

    const path = this.buildPath(queue, partition, namespace, task)
    const baseParams = this.buildParams(batch, wait, timeoutMillis, group, subscriptionMode, subscriptionFrom, namespace, task, autoAck)

    const affinityKey = this.getAffinityKey(queue, partition, namespace, task, group)

    const workers: Promise<void>[] = []
    for (let i = 0; i < concurrency; i++) {
      workers.push(this.worker(i, handler, path, baseParams, {
        batch,
        limit,
        idleMillis,
        autoAck,
        wait,
        timeoutMillis,
        renewLease,
        renewLeaseIntervalMillis,
        each,
        signal,
        group,
        affinityKey
      }))
    }

    logger.log('ConsumerManager.start', { status: 'workers-started', count: concurrency })

    await Promise.all(workers)

    logger.log('ConsumerManager.start', { status: 'completed' })
  }

  private async worker(workerId: number, handler: (msgOrMsgs: QueenMessage | QueenMessage[]) => Promise<unknown>, path: string, baseParams: URLSearchParams, options: WorkerOptions): Promise<void> {
    const {
      batch,
      limit,
      idleMillis,
      autoAck,
      wait,
      timeoutMillis,
      renewLease,
      renewLeaseIntervalMillis,
      each,
      signal,
      group,
      affinityKey
    } = options

    logger.log('ConsumerManager.worker', { workerId, status: 'started', limit, idleMillis })

    let processedCount = 0
    let lastMessageTime = idleMillis ? Date.now() : null

    while (true) {
      if (signal && signal.aborted) {
        logger.log('ConsumerManager.worker', { workerId, status: 'aborted', processedCount })
        break
      }

      if (limit && processedCount >= limit) {
        logger.log('ConsumerManager.worker', { workerId, status: 'limit-reached', processedCount, limit })
        break
      }

      if (idleMillis && lastMessageTime) {
        const idleTime = Date.now() - lastMessageTime
        if (idleTime >= idleMillis) {
          logger.log('ConsumerManager.worker', { workerId, status: 'idle-timeout', processedCount, idleTime })
          break
        }
      }

      try {
        const clientTimeout = wait ? timeoutMillis + 5000 : timeoutMillis
        const result = await this.httpClient.get(`${path}?${baseParams}`, clientTimeout, affinityKey) as { messages?: QueenMessage[] } | null

        if (!result || !result.messages || result.messages.length === 0) {
          if (wait) {
            continue
          } else {
            await new Promise(resolve => setTimeout(resolve, 100))
            continue
          }
        }

        const messages = result.messages.filter((msg): msg is QueenMessage => msg != null)

        if (messages.length === 0) {
          continue
        }

        logger.log('ConsumerManager.worker', { workerId, status: 'messages-received', count: messages.length })

        this.enhanceMessagesWithTrace(messages, group)

        if (idleMillis) {
          lastMessageTime = Date.now()
        }

        let renewalTimer: ReturnType<typeof setInterval> | null = null
        if (renewLease && renewLeaseIntervalMillis) {
          renewalTimer = this.setupLeaseRenewal(messages, renewLeaseIntervalMillis)
        }

        try {
          if (each) {
            for (const message of messages) {
              if (signal && signal.aborted) break

              await this.processMessage(message, handler, autoAck, group)
              processedCount++

              if (limit && processedCount >= limit) break
            }
          } else {
            await this.processBatch(messages, handler, autoAck, group)
            processedCount += messages.length
          }

          logger.log('ConsumerManager.worker', { workerId, status: 'messages-processed', count: messages.length, total: processedCount })
        } finally {
          if (renewalTimer) {
            clearInterval(renewalTimer)
          }
        }

      } catch (error) {
        const err = error as Error & { code?: string }
        const isTimeoutError = err.name === 'AbortError' ||
                              err.message?.includes('timeout')

        if (isTimeoutError && wait) {
          continue
        }

        const isNetworkError = err.message?.includes('fetch failed') ||
                              err.message?.includes('ECONNREFUSED') ||
                              err.code === 'ECONNREFUSED'

        if (isNetworkError) {
          logger.warn('ConsumerManager.worker', { workerId, error: 'network', message: err.message })
          console.warn(`Worker ${workerId}: Network error - ${err.message}`)
          await new Promise(resolve => setTimeout(resolve, 1000))
          continue
        }

        logger.error('ConsumerManager.worker', { workerId, error: err.message })
        throw error
      }
    }

    logger.log('ConsumerManager.worker', { workerId, status: 'stopped', processedCount })
  }

  private async processMessage(message: QueenMessage, handler: (msgOrMsgs: QueenMessage | QueenMessage[]) => Promise<unknown>, autoAck: boolean, group: string | null): Promise<void> {
    try {
      await handler(message)

      if (autoAck) {
        const context = group ? { group } : {}
        await this.queen.ack(message, true, context)
        logger.log('ConsumerManager.processMessage', { transactionId: message.transactionId, status: 'acked' })
      }
    } catch (error) {
      if (autoAck) {
        const context = group ? { group } : {}
        await this.queen.ack(message, false, context)
        logger.error('ConsumerManager.processMessage', { transactionId: message.transactionId, error: (error as Error).message, status: 'nacked' })
        return
      }
      logger.error('ConsumerManager.processMessage', { transactionId: message.transactionId, error: (error as Error).message })
      throw error
    }
  }

  private async processBatch(messages: QueenMessage[], handler: (msgOrMsgs: QueenMessage | QueenMessage[]) => Promise<unknown>, autoAck: boolean, group: string | null): Promise<void> {
    try {
      await handler(messages)

      if (autoAck) {
        const context = group ? { group } : {}
        await this.queen.ack(messages, true, context)
        logger.log('ConsumerManager.processBatch', { count: messages.length, status: 'acked' })
      }
    } catch (error) {
      if (autoAck) {
        const context = group ? { group } : {}
        await this.queen.ack(messages, false, context)
        logger.error('ConsumerManager.processBatch', { count: messages.length, error: (error as Error).message, status: 'nacked' })
        return
      }
      logger.error('ConsumerManager.processBatch', { count: messages.length, error: (error as Error).message })
      throw error
    }
  }

  private setupLeaseRenewal(messages: QueenMessage[], intervalMillis: number): ReturnType<typeof setInterval> | null {
    const leaseIds = messages.map(m => m.leaseId).filter((id): id is string => id != null)

    if (leaseIds.length === 0) return null

    return setInterval(async () => {
      try {
        await this.queen.renew(messages)
      } catch (error) {
        console.error('Lease renewal failed:', error)
      }
    }, intervalMillis)
  }

  private enhanceMessagesWithTrace(messages: QueenMessage[], group: string | null): void {
    const httpClient = this.httpClient
    const consumerGroup = group || '__QUEUE_MODE__'

    for (const message of messages) {
      message.trace = async (traceConfig: TraceConfig) => {
        try {
          if (typeof traceConfig !== 'object' || !traceConfig.data) {
            logger.warn('ConsumerManager.trace', {
              error: 'Invalid trace config: requires { data: {...} }',
              transactionId: message.transactionId
            })
            return { success: false, error: 'Invalid trace config: requires { data: {...} }' }
          }

          let traceNames: string[] | null = null
          if (traceConfig.traceName) {
            if (Array.isArray(traceConfig.traceName)) {
              traceNames = traceConfig.traceName.filter(n => typeof n === 'string' && n.length > 0)
              if (traceNames.length === 0) traceNames = null
            } else if (typeof traceConfig.traceName === 'string') {
              traceNames = [traceConfig.traceName]
            }
          }

          const response = await httpClient.post('/api/v1/traces', {
            transactionId: message.transactionId,
            partitionId: message.partitionId,
            consumerGroup: consumerGroup,
            traceNames: traceNames,
            eventType: traceConfig.eventType || 'info',
            data: traceConfig.data
          })

          logger.log('ConsumerManager.trace', {
            transactionId: message.transactionId,
            success: true,
            traceNames: traceNames
          })
          return { success: true, ...(response as object) }
        } catch (error) {
          logger.error('ConsumerManager.trace', {
            transactionId: message.transactionId,
            error: (error as Error).message,
            phase: 'trace-failed'
          })
          console.warn(`[TRACE FAILED] ${message.transactionId}: ${(error as Error).message}`)

          return { success: false, error: (error as Error).message }
        }
      }
    }
  }

  private buildPath(queue: string | null, partition: string | null, namespace: string | null, task: string | null): string {
    if (queue) {
      if (partition) {
        return `/api/v1/pop/queue/${queue}/partition/${partition}`
      }
      return `/api/v1/pop/queue/${queue}`
    }

    if (namespace || task) {
      return '/api/v1/pop'
    }

    throw new Error('Must specify queue, namespace, or task')
  }

  private buildParams(batch: number, wait: boolean, timeoutMillis: number, group: string | null, subscriptionMode: string | null, subscriptionFrom: string | null, namespace: string | null, task: string | null, _autoAck: boolean): URLSearchParams {
    const params = new URLSearchParams({
      batch: batch.toString(),
      wait: wait.toString(),
      timeout: timeoutMillis.toString()
    })

    if (group) params.append('consumerGroup', group)
    if (subscriptionMode) params.append('subscriptionMode', subscriptionMode)
    if (subscriptionFrom) params.append('subscriptionFrom', subscriptionFrom)
    if (namespace) params.append('namespace', namespace)
    if (task) params.append('task', task)

    return params
  }
}
