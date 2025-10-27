/**
 * Transaction builder for atomic operations
 */

export class TransactionBuilder {
  #httpClient
  #operations = []
  #requiredLeases = []

  constructor(httpClient) {
    this.#httpClient = httpClient
  }

  ack(messages, status = 'completed') {
    const msgs = Array.isArray(messages) ? messages : [messages]

    msgs.forEach(msg => {
      const transactionId = typeof msg === 'string' ? msg : (msg.transactionId || msg.id)
      const leaseId = typeof msg === 'object' ? msg.leaseId : null

      if (!transactionId) {
        throw new Error('Message must have transactionId or id property')
      }

      this.#operations.push({
        type: 'ack',
        transactionId,
        status
      })

      if (leaseId) {
        this.#requiredLeases.push(leaseId)
      }
    })

    return this
  }

  queue(queueName) {
    // Return a sub-builder for push operations
    return {
      push: (items) => {
        const itemArray = Array.isArray(items) ? items : [items]

        this.#operations.push({
          type: 'push',
          items: itemArray.map(item => ({
            queue: queueName,
            payload: item.payload || item
          }))
        })

        return this
      }
    }
  }

  async commit() {
    if (this.#operations.length === 0) {
      throw new Error('Transaction has no operations to commit')
    }

    const result = await this.#httpClient.post('/api/v1/transaction', {
      operations: this.#operations,
      requiredLeases: [...new Set(this.#requiredLeases)] // Unique leases
    })

    if (!result.success) {
      throw new Error(result.error || 'Transaction failed')
    }

    return result
  }
}

