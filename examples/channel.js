import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

const queueName = 'benchmark-4'
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
for (var k = 0; k < 1000; k += 1) {
    let messages = []
    for (var i = 0; i < 100; i += 1) {
        messages.push({
            transactionId: 'trx-' + i,
            data: { message: 'Hello, world! ' + i } 
        })
    }
    await queen
    .queue(queueName)
    .partition('p-' + k)
    .push(messages) 
    console.log('Pushed', k, messages.length)
}



/*const p2 = await queen
.queue(queueName)
.partition('p1')
.push([{ 
    transactionId: '123',
    data: { message: 'Hello, world!' } 
}])

console.log(p2)*/

// Consume message from queue with consumer group and
// atomically ack input and push to another queue
await queen
.queue(queueName)
.group('analytics')
.renewLease(true, 2000) // Renew lease every 2 seconds
.concurrency(10)
.autoAck(false) 
.batch(10)
.each()
.consume(async (message) => {
    // process message
    console.log(message.partitionId, message.id)
    return {
        nextPartition: 'p2' // This will be available in the result object
    }
})
.onSuccess(async (message, result) => {
      // Atomically ack input and push to write queue
    await queen.ack(message, true, { group: 'analytics' })

    /*await queen
    .transaction()
    .ack(message, 'completed', { consumerGroup: 'analytics' })
    .queue(trxQueue)
    .partition(result.nextPartition)
    .push([{
          data: {
            ...result
          }, 
          transactionId: message.transactionId + '-next'
    }])
    .commit()*/

    //console.log('Successfully processed message and pushed to next partition')
})
.onError(async (message, error) => {
    await queen.ack(message, false, { group: 'analytics' })
})

/*await queen
.queue(trxQueue)
.group('final')
.renewLease(true, 2000) // Renew lease every 2 seconds
.autoAck(false) 
.batch(10)
.each()
.consume(async (message) => {
    console.log(message)
})
.onSuccess(async (message, result) => {
    await queen.ack(message, true, { group: 'final' })
})
.onError(async (message, error) => {
    await queen.ack(message, false, { group: 'final' })
})*/
