import { Queen } from '../client-js/client/index.js'
import { v4 as uuid} from 'uuid'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queueTranslations = 'chat-translations'
const queueAgent = 'chat-agent'
await client.queue(queueTranslations, {
    leaseTime: 5
});

await client.queue(queueAgent, {
    leaseTime: 5
});

let i = 0
const produce = async () => {
    while (true) {
        const chatId = uuid()
        await client.push(`${queueTranslations}/${chatId}`, [
            { 
                transactionId: uuid(),
                chatId: chatId,
                event: 'translation-processing'
            }
        ])
        console.log('produced', i)
        await new Promise(resolve => setTimeout(resolve, 5000))
        i += 1
    }
}

produce()

client
.pipeline(`${queueTranslations}`)
.take(1, {
    wait: true
})
.process(async (message) => {
    console.log('translation-processing', message)
    await client.push(`${queueAgent}/${message.data.chatId}`, [ message.data ])
    return message
})
.repeat({ continuous: true })
.execute()

client
.pipeline(`${queueAgent}`)
.take(1, {
    wait: true
})
.process(async (message) => {
    console.log('agent-processing', message)
    return message
})
.onError(async (error, message) => {
    await client.ack(message, false)
})
.repeat({ continuous: true })
.execute()