import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

const queueName = 'test-queue'
const trxQueue = 'trx-queue'

// Delete to start fresh
await queen
.queue(queueName)
.delete()

// Create queue with config
await queen
.queue(queueName)
.config({
    leaseTime: 10, // 10 seconds to process
    retryLimit: 3,
    retentionSeconds: 86400,
    encryptionEnabled: false
})
.create()

// Push message to queue
await queen
.queue(queueName)
.partition('p1')
.push([{ 
    transactionId: '123',
    data: { message: 'Hello, world!' } 
}])

// Consume message from queue with consumer group and
// atomically ack input and push to another queue
await queen
.queue(queueName)
.group('analytics')
.autoAck(false) 
.batch(10)
.each()
.consume(async (message) => {
    // process message
    return {
        nextPartition: 'p2' // This will be available in the result object
    }
})
.onSuccess(async (message, result) => {
      // Atomically ack input and push to write queue
    await queen
    .transaction()
    .ack(message, 'completed', { consumerGroup: 'analytics' })
    .queue(trxQueue)
    .partition(message.nextPartition)
    .push([{
          data: {
            ...result
          }, 
          transactionId: message.transactionId + '-next'
    }])
    .commit()
    console.log('Successfully processed message and pushed to next partition')
})
.onError(async (message, error) => {
    await queen.ack(message, false, { group: 'analytics' })
})