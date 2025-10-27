import { Queen } from '../client-js/client/index.js'
import { v4 as uuid} from 'uuid'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'chat-agent-events'
await client.queue(queue, {
    leaseTime: 5
});

let i = 0
const produce = async () => {
    while (true) {
        const chatId = uuid()
        await client.push(`${queue}`, [
            { 
                transactionId: uuid(),
                chatId: i,
                event: 'agent-processing'
            }
        ])
        console.log('produced', i)
        await new Promise(resolve => setTimeout(resolve, 10))
        i += 1
    }
}

produce()

client
.pipeline(`${queue}@ws-0`)
.take(10, {
    wait: true
})
.processBatch(async (message) => {
    console.log('ws-0', message.map(x => `${x.data.chatId}, ${x.data.event}`))
    return message
})
.repeat({ continuous: true })
.execute()

client
.pipeline(`${queue}@ws-1`)
.take(10, {
    wait: true
})
.processBatch(async (message) => {
    console.log('ws-1', message.map(x => `${x.data.chatId}, ${x.data.event}`))
    return message
})
.repeat({ continuous: true })
.execute()