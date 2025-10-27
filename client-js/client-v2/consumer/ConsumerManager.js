/**
 * Consumer manager for handling concurrent workers
 */

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

    // Build the path and params for pop requests
    const path = this.#buildPath(queue, partition, namespace, task)
    const baseParams = this.#buildParams(batch, wait, timeoutMillis, group, subscriptionMode, subscriptionFrom, namespace, task, autoAck)

    // Start workers
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
        signal
      }))
    }

    // Wait for all workers to complete
    await Promise.all(workers)
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
      signal
    } = options

    let processedCount = 0
    let lastMessageTime = idleMillis ? Date.now() : null

    while (true) {
      // Check abort signal
      if (signal && signal.aborted) {
        break
      }

      // Check limit
      if (limit && processedCount >= limit) {
        break
      }

      // Check idle timeout
      if (idleMillis && lastMessageTime) {
        const idleTime = Date.now() - lastMessageTime
        if (idleTime >= idleMillis) {
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

              await this.#processMessage(message, handler, autoAck)
              processedCount++

              if (limit && processedCount >= limit) break
            }
          } else {
            // Process as batch
            await this.#processBatch(messages, handler, autoAck)
            processedCount += messages.length
          }
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
          console.warn(`Worker ${workerId}: Network error - ${error.message}`)
          // Wait before retry
          await new Promise(resolve => setTimeout(resolve, 1000))
          continue
        }

        // Other errors - rethrow
        throw error
      }
    }
  }

  async #processMessage(message, handler, autoAck) {
    try {
      await handler(message)

      // Auto-ack on success if enabled
      if (autoAck) {
        await this.#queen.ack(message, true)
      }
    } catch (error) {
      // Auto-nack on error if enabled
      if (autoAck) {
        await this.#queen.ack(message, false)
      }
      throw error
    }
  }

  async #processBatch(messages, handler, autoAck) {
    try {
      await handler(messages)

      // Auto-ack on success if enabled
      if (autoAck) {
        await this.#queen.ack(messages, true)
      }
    } catch (error) {
      // Auto-nack on error if enabled
      if (autoAck) {
        await this.#queen.ack(messages, false)
      }
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
    if (autoAck) params.append('autoAck', 'true')

    return params
  }
}

