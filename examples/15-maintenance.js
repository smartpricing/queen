import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'req-reply-1'
const reqId = '123'

await queen
.queue(queueName)
.config({
    leaseTime: 10,
})
.create()

async function consumer() {
    await queen
    .queue(queueName)
    .partition(`request`)
    .each()
    .consume(async (message) => {
        console.log('Received:', message.data.id)
    })
}


consumer()
if (process.argv[2] === 'producer') {
    let i = 0
    while (true) {
        await queen
        .queue(queueName)
        .partition(`request`)
        .push([
        { data: { id: i, message: 'Hello', responseId: reqId } }
        ])
        await new Promise(resolve => setTimeout(resolve, 1000))
        i++
    }
} 

