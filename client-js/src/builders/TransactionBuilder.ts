/**
 * Transaction builder for atomic operations
 */

import * as logger from '../utils/logger'
import type { HttpClient } from '../http/HttpClient'
import type { QueenMessage, TransactionOperation } from '../types'

export class TransactionBuilder {
  private httpClient: HttpClient
  private operations: TransactionOperation[] = []
  private requiredLeases: string[] = []

  constructor(httpClient: HttpClient) {
    this.httpClient = httpClient
  }

  ack(messages: QueenMessage | QueenMessage[] | string | string[], status: string = 'completed', context: { consumerGroup?: string } = {}): this {
    const msgs = Array.isArray(messages) ? messages : [messages]

    logger.log('TransactionBuilder.ack', { count: msgs.length, status, consumerGroup: context.consumerGroup })

    msgs.forEach(msg => {
      const transactionId = typeof msg === 'string' ? msg : ((msg as QueenMessage).transactionId || (msg as QueenMessage).id)
      const partitionId = typeof msg === 'object' ? (msg as QueenMessage).partitionId : null
      const leaseId = typeof msg === 'object' ? (msg as QueenMessage).leaseId : null

      if (!transactionId) {
        throw new Error('Message must have transactionId or id property')
      }

      if (!partitionId) {
        throw new Error('Message must have partitionId property to ensure message uniqueness')
      }

      const operation: TransactionOperation = {
        type: 'ack',
        transactionId,
        partitionId,
        status
      }

      if (context.consumerGroup) {
        operation.consumerGroup = context.consumerGroup
      }

      this.operations.push(operation)

      if (leaseId) {
        this.requiredLeases.push(leaseId)
      }
    })

    return this
  }

  queue(queueName: string): { partition: (partitionKey: string) => { partition: (partitionKey: string) => unknown; push: (items: unknown | unknown[]) => TransactionBuilder }; push: (items: unknown | unknown[]) => TransactionBuilder } {
    let partition: string | null = null

    const subBuilder = {
      partition: (partitionKey: string) => {
        partition = partitionKey
        return subBuilder
      },

      push: (items: unknown | unknown[]) => {
        const itemArray = Array.isArray(items) ? items : [items]

        logger.log('TransactionBuilder.queue.push', { queue: queueName, partition, count: itemArray.length })

        this.operations.push({
          type: 'push',
          items: itemArray.map(item => {
            let payloadValue: unknown
            const obj = item as Record<string, unknown>
            if (typeof item === 'object' && item !== null && 'data' in obj) {
              payloadValue = obj.data
            } else if (typeof item === 'object' && item !== null && 'payload' in obj) {
              payloadValue = obj.payload
            } else {
              payloadValue = item
            }

            const result: { queue: string; payload: unknown; partition?: string } = {
              queue: queueName,
              payload: payloadValue
            }

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

  async commit(): Promise<unknown> {
    if (this.operations.length === 0) {
      logger.error('TransactionBuilder.commit', 'No operations to commit')
      throw new Error('Transaction has no operations to commit')
    }

    logger.log('TransactionBuilder.commit', { operationCount: this.operations.length, requiredLeases: this.requiredLeases.length })

    try {
      const result = await this.httpClient.post('/api/v1/transaction', {
        operations: this.operations,
        requiredLeases: [...new Set(this.requiredLeases)]
      }) as { success?: boolean; error?: string }

      if (!result.success) {
        logger.error('TransactionBuilder.commit', { error: result.error })
        throw new Error(result.error || 'Transaction failed')
      }

      logger.log('TransactionBuilder.commit', { status: 'success' })
      return result
    } catch (error) {
      logger.error('TransactionBuilder.commit', { error: (error as Error).message })
      throw error
    }
  }
}
