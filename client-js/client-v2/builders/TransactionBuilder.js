/**
 * Transaction builder for atomic operations
 */

import * as logger from '../utils/logger.js'

export class TransactionBuilder {
  #httpClient
  #operations = []
  #requiredLeases = []

  constructor(httpClient) {
    this.#httpClient = httpClient
  }

  ack(messages, status = 'completed') {
    const msgs = Array.isArray(messages) ? messages : [messages]
    
    logger.log('TransactionBuilder.ack', { count: msgs.length, status })

    msgs.forEach(msg => {
      const transactionId = typeof msg === 'string' ? msg : (msg.transactionId || msg.id)
      const partitionId = typeof msg === 'object' ? msg.partitionId : null
      const leaseId = typeof msg === 'object' ? msg.leaseId : null

      if (!transactionId) {
        throw new Error('Message must have transactionId or id property')
      }

      // CRITICAL: partitionId is now MANDATORY to prevent acking wrong message
      if (!partitionId) {
        throw new Error('Message must have partitionId property to ensure message uniqueness')
      }

      this.#operations.push({
        type: 'ack',
        transactionId,
        partitionId,
        status
      })

      if (leaseId) {
        this.#requiredLeases.push(leaseId)
      }
    })

    return this
  }

  queue(queueName) {
    // Return a sub-builder for push operations with partition support
    let partition = null
    
    const subBuilder = {
      partition: (partitionKey) => {
        partition = partitionKey
        return subBuilder
      },
      
      push: (items) => {
        const itemArray = Array.isArray(items) ? items : [items]
        
        logger.log('TransactionBuilder.queue.push', { queue: queueName, partition, count: itemArray.length })

        this.#operations.push({
          type: 'push',
          items: itemArray.map(item => {
            // Check if property exists, not just truthy (to support null values)
            let payloadValue
            if ('data' in item) {
              payloadValue = item.data
            } else if ('payload' in item) {
              payloadValue = item.payload
            } else {
              payloadValue = item
            }

            const result = {
              queue: queueName,
              payload: payloadValue
            }
            
            // Add partition if set
            if (partition !== null) {
              result.partition = partition
            }

            return result
          })
        })

        return this
      }
    }
    
    return subBuilder
  }

  async commit() {
    if (this.#operations.length === 0) {
      logger.error('TransactionBuilder.commit', 'No operations to commit')
      throw new Error('Transaction has no operations to commit')
    }

    logger.log('TransactionBuilder.commit', { operationCount: this.#operations.length, requiredLeases: this.#requiredLeases.length })

    try {
      const result = await this.#httpClient.post('/api/v1/transaction', {
        operations: this.#operations,
        requiredLeases: [...new Set(this.#requiredLeases)] // Unique leases
      })

      if (!result.success) {
        logger.error('TransactionBuilder.commit', { error: result.error })
        throw new Error(result.error || 'Transaction failed')
      }

      logger.log('TransactionBuilder.commit', { status: 'success' })
      return result
    } catch (error) {
      logger.error('TransactionBuilder.commit', { error: error.message })
      throw error
    }
  }
}

