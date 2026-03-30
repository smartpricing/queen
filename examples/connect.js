import { Queen } from 'queen-mq'
const queen = new Queen('http://localhost:6632')

const sendgridQueue = 'connect.newsletter.sendgrid'

// Create queue with config
await queen
.queue(sendgridQueue)
.config({
    leaseTime: 100, // 10 seconds to process
    retryLimit: 3,
    retentionSeconds: 86400,
    encryptionEnabled: false
})
.create()


// Push message to queue
await queen
.queue(sendgridQueue)
.partition('p1')
.push([{ 
    transactionId: '123',
    data: { message: 'Hello, world!' } 
}])


// Pop message to queue
await queen
.queue(sendgridQueue)
.group('sender')
.renewLease(true, 2000) // Renew lease every 2 seconds
.autoAck(true) 
.batch(100)
.each()
.consume(async (message) => {
    try {
        console.log(message.data)
    } catch (error) {
        console.log(new Date(), 'Error at sender', error)
    }
})

