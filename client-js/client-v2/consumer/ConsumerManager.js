/**
 * Consumer manager for handling concurrent workers
 */

import * as logger from '../utils/logger.js'

export class ConsumerManager {
  #httpClient
  #queen

  constructor(httpClient, queen) {
    this.#httpClient = httpClient
    this.#queen = queen
  }

  async start(handler, options) {
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

    // Build the path and params for pop requests
    const path = this.#buildPath(queue, partition, namespace, task)
    const baseParams = this.#buildParams(batch, wait, timeoutMillis, group, subscriptionMode, subscriptionFrom, namespace, task, autoAck)

    // Use worker pool pattern if concurrency > 1 and each mode is enabled
    // This maintains constant concurrency by immediately spawning new workers
    if (concurrency > 1 && each) {
      logger.log('ConsumerManager.start', { mode: 'worker-pool', concurrency })
      await this.#workerPool(handler, path, baseParams, {
        concurrency,
        batch,
        limit,
        idleMillis,
        autoAck,
        wait,
        timeoutMillis,
        renewLease,
        renewLeaseIntervalMillis,
        signal,
        group
      })
    } else {
      // Use traditional parallel workers for batch mode or single worker
      logger.log('ConsumerManager.start', { mode: 'parallel-workers', concurrency })
      const workers = []
      for (let i = 0; i < concurrency; i++) {
        workers.push(this.#worker(i, handler, path, baseParams, {
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
          group
        }))
      }

      logger.log('ConsumerManager.start', { status: 'workers-started', count: concurrency })
      await Promise.all(workers)
    }
    
    logger.log('ConsumerManager.start', { status: 'completed' })
  }

  /**
   * Worker pool pattern - maintains constant concurrency
   * Continuously polls for messages and dispatches to worker pool
   */
  async #workerPool(handler, path, baseParams, options) {
    const {
      concurrency,
      batch,
      limit,
      idleMillis,
      autoAck,
      wait,
      timeoutMillis,
      renewLease,
      renewLeaseIntervalMillis,
      signal,
      group
    } = options

    // Message queue for pending work
    const messageQueue = []
    let processedCount = 0
    let activeWorkers = 0
    let polling = true
    let lastMessageTime = idleMillis ? Date.now() : null

    // Resolve when all workers complete
    let resolveComplete
    const completionPromise = new Promise(resolve => { resolveComplete = resolve })

    // Worker function that processes one message then exits
    const processOneMessage = async (message, workerId) => {
      activeWorkers++
      logger.log('ConsumerManager.workerPool', { workerId, status: 'processing', activeWorkers, queueSize: messageQueue.length })
      
      try {
        await this.#processMessage(message, handler, autoAck, group)
        processedCount++
        
        logger.log('ConsumerManager.workerPool', { workerId, status: 'completed', processedCount, activeWorkers })
      } catch (error) {
        logger.error('ConsumerManager.workerPool', { workerId, error: error.message })
      } finally {
        activeWorkers--
        
        // Check if we should stop
        if (limit && processedCount >= limit) {
          polling = false
        }
        
        // Try to spawn another worker if there's work available
        if (messageQueue.length > 0 && polling) {
          const nextMessage = messageQueue.shift()
          if (nextMessage) {
            // Spawn new worker immediately (don't await)
            processOneMessage(nextMessage, workerId).catch(err => {
              logger.error('ConsumerManager.workerPool', { workerId, error: err.message })
            })
          }
        }
        
        // If no more work and not polling, signal completion
        if (!polling && activeWorkers === 0 && messageQueue.length === 0) {
          resolveComplete()
        }
      }
    }

    // Poller function - continuously fetches messages
    const poller = async () => {
      let nextWorkerId = 0
      
      while (polling) {
        // Check abort signal
        if (signal && signal.aborted) {
          logger.log('ConsumerManager.workerPool', { status: 'aborted', processedCount })
          polling = false
          break
        }

        // Check limit
        if (limit && processedCount >= limit) {
          logger.log('ConsumerManager.workerPool', { status: 'limit-reached', processedCount, limit })
          polling = false
          break
        }

        // Check idle timeout
        if (idleMillis && lastMessageTime) {
          const idleTime = Date.now() - lastMessageTime
          if (idleTime >= idleMillis) {
            logger.log('ConsumerManager.workerPool', { status: 'idle-timeout', processedCount, idleTime })
            polling = false
            break
          }
        }

        try {
          // Poll for messages
          const clientTimeout = wait ? timeoutMillis + 5000 : timeoutMillis
          const result = await this.#httpClient.get(`${path}?${baseParams}`, clientTimeout)

          // Handle empty response
          if (!result || !result.messages || result.messages.length === 0) {
            if (wait) {
              continue // Long polling timeout, retry
            } else {
              await new Promise(resolve => setTimeout(resolve, 100))
              continue
            }
          }

          const messages = result.messages.filter(msg => msg != null)
          if (messages.length === 0) {
            continue
          }

          logger.log('ConsumerManager.workerPool', { status: 'messages-received', count: messages.length, queueSize: messageQueue.length, activeWorkers })

          // Update last message time
          if (idleMillis) {
            lastMessageTime = Date.now()
          }

          // Set up lease renewal for all messages if enabled
          if (renewLease && renewLeaseIntervalMillis) {
            const renewalTimer = this.#setupLeaseRenewal(messages, renewLeaseIntervalMillis)
            // Clear timer after processing window
            setTimeout(() => clearInterval(renewalTimer), renewLeaseIntervalMillis * 3)
          }

          // Dispatch messages to worker pool
          for (const message of messages) {
            if (!polling) break
            if (limit && processedCount >= limit) {
              polling = false
              break
            }

            // If we have available worker slots, spawn immediately
            if (activeWorkers < concurrency) {
              processOneMessage(message, nextWorkerId++).catch(err => {
                logger.error('ConsumerManager.workerPool', { error: err.message })
              })
            } else {
              // Otherwise queue for later
              messageQueue.push(message)
            }
          }

        } catch (error) {
          // Check if this is a timeout error (expected for long polling)
          const isTimeoutError = error.name === 'AbortError' ||
                                error.message?.includes('timeout')

          if (isTimeoutError && wait) {
            continue
          }

          // Check if network error
          const isNetworkError = error.message?.includes('fetch failed') ||
                                error.message?.includes('ECONNREFUSED') ||
                                error.code === 'ECONNREFUSED'

          if (isNetworkError) {
            logger.warn('ConsumerManager.workerPool', { error: 'network', message: error.message })
            console.warn(`Worker pool: Network error - ${error.message}`)
            await new Promise(resolve => setTimeout(resolve, 1000))
            continue
          }

          // Other errors - log and stop
          logger.error('ConsumerManager.workerPool', { error: error.message })
          polling = false
          break
        }
      }

      // Wait for all active workers to complete
      logger.log('ConsumerManager.workerPool', { status: 'polling-stopped', activeWorkers, queueSize: messageQueue.length })
    }

    // Start poller
    poller().catch(err => {
      logger.error('ConsumerManager.workerPool', { error: err.message })
      polling = false
    })

    // Wait for completion
    await completionPromise
    
    logger.log('ConsumerManager.workerPool', { status: 'completed', processedCount })
  }

  async #worker(workerId, handler, path, baseParams, options) {
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
      group
    } = options

    logger.log('ConsumerManager.worker', { workerId, status: 'started', limit, idleMillis })
    
    let processedCount = 0
    let lastMessageTime = idleMillis ? Date.now() : null

    while (true) {
      // Check abort signal
      if (signal && signal.aborted) {
        logger.log('ConsumerManager.worker', { workerId, status: 'aborted', processedCount })
        break
      }

      // Check limit
      if (limit && processedCount >= limit) {
        logger.log('ConsumerManager.worker', { workerId, status: 'limit-reached', processedCount, limit })
        break
      }

      // Check idle timeout
      if (idleMillis && lastMessageTime) {
        const idleTime = Date.now() - lastMessageTime
        if (idleTime >= idleMillis) {
          logger.log('ConsumerManager.worker', { workerId, status: 'idle-timeout', processedCount, idleTime })
          break
        }
      }

      try {
        // Pop messages
        const clientTimeout = wait ? timeoutMillis + 5000 : timeoutMillis
        const result = await this.#httpClient.get(`${path}?${baseParams}`, clientTimeout)

        // Handle empty response
        if (!result || !result.messages || result.messages.length === 0) {
          if (wait) {
            continue // Long polling timeout, retry
          } else {
            // Short delay before retry
            await new Promise(resolve => setTimeout(resolve, 100))
            continue
          }
        }

        const messages = result.messages.filter(msg => msg != null)

        if (messages.length === 0) {
          continue
        }

        logger.log('ConsumerManager.worker', { workerId, status: 'messages-received', count: messages.length })

        // Update last message time
        if (idleMillis) {
          lastMessageTime = Date.now()
        }

        // Set up lease renewal if enabled
        let renewalTimer = null
        if (renewLease && renewLeaseIntervalMillis) {
          renewalTimer = this.#setupLeaseRenewal(messages, renewLeaseIntervalMillis)
        }

        try {
          // Process messages
          if (each) {
            // Process one at a time
            for (const message of messages) {
              if (signal && signal.aborted) break

              await this.#processMessage(message, handler, autoAck, group)
              processedCount++

              if (limit && processedCount >= limit) break
            }
          } else {
            // Process as batch
            await this.#processBatch(messages, handler, autoAck, group)
            processedCount += messages.length
          }
          
          logger.log('ConsumerManager.worker', { workerId, status: 'messages-processed', count: messages.length, total: processedCount })
        } finally {
          // Clear renewal timer
          if (renewalTimer) {
            clearInterval(renewalTimer)
          }
        }

      } catch (error) {
        // Check if this is a timeout error (expected for long polling)
        const isTimeoutError = error.name === 'AbortError' ||
                              error.message?.includes('timeout')

        if (isTimeoutError && wait) {
          continue // Retry on timeout
        }

        // Check if network error
        const isNetworkError = error.message?.includes('fetch failed') ||
                              error.message?.includes('ECONNREFUSED') ||
                              error.code === 'ECONNREFUSED'

        if (isNetworkError) {
          logger.warn('ConsumerManager.worker', { workerId, error: 'network', message: error.message })
          console.warn(`Worker ${workerId}: Network error - ${error.message}`)
          // Wait before retry
          await new Promise(resolve => setTimeout(resolve, 1000))
          continue
        }

        // Other errors - rethrow
        logger.error('ConsumerManager.worker', { workerId, error: error.message })
        throw error
      }
    }
    
    logger.log('ConsumerManager.worker', { workerId, status: 'stopped', processedCount })
  }

  async #processMessage(message, handler, autoAck, group) {
    try {
      await handler(message)

      // Auto-ack on success if enabled
      if (autoAck) {
        const context = group ? { group } : {}
        await this.#queen.ack(message, true, context)
        logger.log('ConsumerManager.processMessage', { transactionId: message.transactionId, status: 'acked' })
      }
    } catch (error) {
      // Auto-nack on error if enabled
      if (autoAck) {
        const context = group ? { group } : {}
        await this.#queen.ack(message, false, context)
        logger.error('ConsumerManager.processMessage', { transactionId: message.transactionId, error: error.message, status: 'nacked' })
        // Don't rethrow when autoAck is enabled - NACK was already sent
        // This allows the consumer to continue and retry
        return
      }
      logger.error('ConsumerManager.processMessage', { transactionId: message.transactionId, error: error.message })
      throw error
    }
  }

  async #processBatch(messages, handler, autoAck, group) {
    try {
      await handler(messages)

      // Auto-ack on success if enabled
      if (autoAck) {
        const context = group ? { group } : {}
        await this.#queen.ack(messages, true, context)
        logger.log('ConsumerManager.processBatch', { count: messages.length, status: 'acked' })
      }
    } catch (error) {
      // Auto-nack on error if enabled
      if (autoAck) {
        const context = group ? { group } : {}
        await this.#queen.ack(messages, false, context)
        logger.error('ConsumerManager.processBatch', { count: messages.length, error: error.message, status: 'nacked' })
        // Don't rethrow when autoAck is enabled - NACK was already sent
        // This allows the consumer to continue and retry
        return
      }
      logger.error('ConsumerManager.processBatch', { count: messages.length, error: error.message })
      throw error
    }
  }

  #setupLeaseRenewal(messages, intervalMillis) {
    const leaseIds = messages.map(m => m.leaseId).filter(id => id != null)

    if (leaseIds.length === 0) return null

    return setInterval(async () => {
      try {
        await this.#queen.renew(messages)
      } catch (error) {
        console.error('Lease renewal failed:', error)
      }
    }, intervalMillis)
  }

  #buildPath(queue, partition, namespace, task) {
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

  #buildParams(batch, wait, timeoutMillis, group, subscriptionMode, subscriptionFrom, namespace, task, autoAck) {
    const params = new URLSearchParams({
      batch: batch.toString(),
      wait: wait.toString(),
      timeout: timeoutMillis.toString()  // Server expects 'timeout', not 'timeoutMillis'
    })

    if (group) params.append('consumerGroup', group)
    if (subscriptionMode) params.append('subscriptionMode', subscriptionMode)
    if (subscriptionFrom) params.append('subscriptionFrom', subscriptionFrom)
    if (namespace) params.append('namespace', namespace)
    if (task) params.append('task', task)
    // NEVER send autoAck for consume - client always manages acking
    // autoAck is only for pop() where server auto-acks immediately

    return params
  }
}

